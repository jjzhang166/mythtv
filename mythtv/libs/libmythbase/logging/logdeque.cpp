/*
 *  Class LogDeque
 *
 *  Copyright (C) Daniel Thor Kristjansson 2013
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Make sure we don't include verbosedefs.h until later.. */
#define VERBOSEDEFS_H_

#include <unistd.h> // for usleep

#include <algorithm> // for stable_sort
using namespace std;

#include <QStringList>
#include <QThread>
#include <QDir>

#include "logeventhandler.h"
#include "mythbaseutil.h"
#include "loghandler.h"
#include "logdeque.h"
#include "mthread.h"

LogDeque LogDeque::s_logDeque;

void LogDeque::InitializeLogging(
    uint64_t verbose_mask,
    int log_level,
    SyslogFacility syslog_facility,
    bool use_threads,
    const QString &logfile,
    const QString &logprefix)
{
    QWriteLocker filter_locker(&m_filterLock);
    m_loggingInitialized = true;
    m_verboseMask = verbose_mask;
    m_logLevel = log_level;
    filter_locker.unlock();

    QWriteLocker handler_locker(&m_handlerLock);

    m_handlers.push_back(LogHandler::GetConsoleHandler());

    if (!logfile.isEmpty())
    {
        m_handlers.push_back(LogHandler::GetFileHandler(logfile));
        m_logFile = logfile;
    }

    if (!logprefix.isEmpty())
    {
        m_handlers.push_back(LogHandler::GetPathHandler(logprefix));
        m_logPrefix = QDir::cleanPath(QDir::fromNativeSeparators(logprefix));
        m_logPath = m_logPrefix.mid(0, m_logPrefix.lastIndexOf("/"));
    }

    if (kNoFacility != syslog_facility)
    {
        m_handlers.push_back(LogHandler::GetSyslogHandler(syslog_facility));
        m_logSyslogFacility = syslog_facility;
    }

    handler_locker.unlock();

    QWriteLocker thread_locker(&m_threadLock);
    if (use_threads)
    {
        m_singleThreaded = false;
        m_thread = new MThread("Logging");
        m_thread->start();
        m_eventHandler = new LogEventHandler(*this);
        m_eventHandler->moveToThread(m_thread->qthread());
        m_eventHandler->Notify();
    }
}

void LogDeque::ThreadShutdown(void)
{
    QWriteLocker thread_locker(&m_threadLock);

    if (m_singleThreaded)
        return;

    m_singleThreaded = true;

    m_thread->quit();
    m_thread->wait();

    delete m_thread;
    delete m_eventHandler;

    m_thread = NULL;
    m_eventHandler = NULL;

    ProcessQueue();
}

void LogDeque::LogLine(const LogEntry &logEntry)
{
    QMutexLocker locker(&m_messagesLock);
    if (uint(m_messages.size()) < m_messagesHardThreshold)
    {
        m_messages.push_back(logEntry);
        if (uint(m_messages.size()) >= m_messagesSlowThreshold)
        {
            if (!m_messagesSleep)
                m_messagesSleep = 5;
            else if (m_messagesSleep < 5000)
                m_messagesSleep++;
        }
        else if (uint(m_messages.size()) <= m_messagesFastThreshold)
        {
            if (m_messagesSleep)
                m_messagesSleep--;
        }
    }
    else
    {
        m_messagesSleep = 0;
    }
    locker.unlock();

    QReadLocker thread_locker(&m_threadLock);
    if (m_singleThreaded)
    {
        ProcessQueue();
    }
    else
    {
        m_eventHandler->Notify();
        if (m_messagesSleep)
            usleep(m_messagesSleep);
    }
}


// This is a version of Dan Bernstein's hash.
//  * with xor rather than addition
//  * with hash * 33 rather than (hash<<5 + hash)
static uint32_t djb_hash(const char *c_str)
{
    uint32_t hash = 5381;
    int c;

    while ((c = *c_str++))
        hash = (hash * 33) ^ c;

    return hash;
}

uint32_t LogDeque::HashString(const char *c_str)
{
    int32_t hash = static_cast<int32_t>(djb_hash(c_str));
    {
        QReadLocker read_locker(&m_hashLock);
        if (m_hashMap.contains(hash))
            return hash;
    }
    QWriteLocker write_locker(&m_hashLock);
    m_hashMap[hash] = QString::fromUtf8(c_str);
    return hash;
}

void LogDeque::ProcessQueue(bool force)
{
    uint64_t verboseMask;
    int logLevel;
    bool loggingInitialized;

    GetLogFilter(verboseMask, logLevel, loggingInitialized);

    if (!loggingInitialized && !force)
        return;

    QList<LogEntry> first_few;
    do
    {
        first_few.clear();

        {
            QMutexLocker locker(&m_messagesLock);
            for (uint i = 0; (i < 16) && !m_messages.empty(); i++)
            {
                first_few.push_back(m_messages.front());
                m_messages.pop_front();
            }
        }

        QReadLocker locker(&m_handlerLock);
        QList<LogEntry>::const_iterator mit = first_few.begin();
        for (; mit != first_few.end(); ++mit)
        {
            if (((verboseMask & (*mit).GetMask()) == (*mit).GetMask()) &&
                (logLevel >= (*mit).GetLevel()))
            {
                QList<LogHandler*>::iterator hit = m_handlers.begin();
                for (; hit != m_handlers.end(); ++hit)
                    (*hit)->HandleLog(*mit);
            }
            else if ((*mit).IsPrint())
            {
                QList<LogHandler*>::iterator hit = m_handlers.begin();
                for (; hit != m_handlers.end(); ++hit)
                    (*hit)->HandlePrint(*mit);
            }
        }

        // Periodically flush
        QList<LogHandler*>::iterator hit = m_handlers.begin();
        for (; hit != m_handlers.end(); ++hit)
            (*hit)->Flush();
    }
    while (!first_few.empty());
}

LogDeque::LogDeque() :
    m_messagesSleep(0),
    m_messagesFastThreshold(49000),
    m_messagesSlowThreshold(50000),
    m_messagesHardThreshold(100000),
    m_loggingInitialized(false),
    m_logLevel(INT_MAX),
    m_verboseMask(~(uint64_t(0))),
    m_singleThreaded(true),
    m_thread(NULL),
    m_eventHandler(NULL)
{
/* We initialize the log level and verbose mask so that before
 * logging is initialized all messages are are passed through to
 * log_line.
 */

/* This calls AddVerbose() and AddLogLevel for everything in verbosedefs.h */

#undef VERBOSEDEFS_H_
#define _IMPLEMENT_VERBOSE
#define VERBOSE_MAP(NAME, MASK, ADDITIVE, HELP)                     \
    m_verboseParseInfo[QString(#NAME)] = m_verboseInfo[MASK] =      \
        VerboseInfo(MASK, QString(#NAME), ADDITIVE, QString(HELP));
#define LOGLEVEL_MAP(NAME, VALUE, CHAR_NAME, PARSE)                 \
    m_logLevelInfo[VALUE] =                                         \
        LogLevelInfo(VALUE, QString(#NAME), QChar(CHAR_NAME));      \
    if (PARSE)                                                      \
        m_logLevelParseInfo[QString(#NAME)] =                       \
            LogLevelInfo(VALUE, QString(#NAME), QChar(CHAR_NAME));
#include "verbosedefs.h"
#undef _IMPLEMENT_VERBOSE

/*
  QHash<QString, VerboseInfo>::const_iterator it =
  m_verboseParseInfo.begin();
  for (; it != m_verboseParseInfo.end(); ++it)
  cout << qPrintable(
  QString("%1 -> %2").arg(it.key(), 12).arg((*it).toString()))
  << endl;
*/
/*
  QHash<QString, LogLevelInfo>::const_iterator it =
  m_logLevelParseInfo.begin();
  for (; it != m_logLevelParseInfo.end(); ++it)
  cout << qPrintable(
  QString("%1 -> %2").arg(it.key(), 11).arg((*it).toString()))
  << endl;
*/

    RegisterThread("UI");
}

LogDeque::~LogDeque()
{
    // This should have already been called, but better safe than sorry.
    ThreadShutdown();

    ProcessQueue(/*force*/ true);

    QWriteLocker handler_locker(&m_handlerLock);
    while (!m_handlers.empty())
    {
        delete m_handlers.back();
        m_handlers.pop_back();
    }
}

QString LogDeque::RegisterThread(const QString &name)
{
    Qt::HANDLE handle = QThread::currentThreadId();
    ThreadInfo ti(name, handle, get_gdb_thread_id());

    QWriteLocker locker(&m_hashLock);
    ThreadInfo old = m_threadInfoMap[handle];
    m_threadInfoMap[handle] = ti;
    return old.GetName();
}

QString LogDeque::RenameThread(const QString &name)
{
    QWriteLocker locker(&m_hashLock);
    Qt::HANDLE handle = QThread::currentThreadId();    
    QHash<Qt::HANDLE, ThreadInfo>::iterator it = m_threadInfoMap.find(handle);
    if (it != m_threadInfoMap.end())
    {
        QString old_name = (*it).GetName();
        m_threadInfoMap[handle] = ThreadInfo(
            name, (*it).GetThreadId(), (*it).GetProcessId());
        return old_name;
    }
    else
    {
        locker.unlock();
        return RegisterThread(name);
    }
}

QString LogDeque::DeregisterThread(void)
{
    QWriteLocker locker(&m_hashLock);
    QHash<Qt::HANDLE, ThreadInfo>::iterator it =
        m_threadInfoMap.find(QThread::currentThreadId());
    if (it != m_threadInfoMap.end())
    {
        QString name = (*it).GetName();
        m_threadInfoMap.erase(it);
        return name;
    }
    return QString();
}

static QString get_verbose_format(
    const QHash<uint64_t, VerboseInfo> &map, uint64_t flag)
{
    QHash<uint64_t, VerboseInfo>::const_iterator it;
    it = map.find(flag);
    if (it == map.end())
        return QString();
    return (*it).GetName().mid(3).toLower();
}

QString LogDeque::FormatVerbose(uint64_t mask)
{
    QStringList list;

    for (uint i = 0; i < 64; i++)
    {
        uint64_t flag = mask & (1ULL<<i);
        if (flag)
            list += get_verbose_format(m_verboseInfo, flag);
    }

    if (list.empty()) // special case for 'none'
        return get_verbose_format(m_verboseInfo, 0);

    return list.join(",");
}

static inline bool lt_vil(const VerboseInfo &a, const VerboseInfo &b)
{
    if (a.IsAdditive() != b.IsAdditive())
        return b.IsAdditive();

    if (a.GetName() != b.GetName())
        return a.GetName() < b.GetName();

    if (a.GetMask() != b.GetMask())
        return a.GetMask() < b.GetMask();

    return a.GetHelp() < b.GetHelp();
}

static inline QString format_help_line(const VerboseInfo &vi)
{
    return vi.GetHelp().isEmpty() ? QString() :
        QString("  %1 - %2\n")
        .arg(vi.GetName().mid(3).toLower(), -15, ' ').arg(vi.GetHelp());
}

QString LogDeque::GetVerboseHelp(void) const
{
    QString msg =
        "The -v/--verbose option accepts a number of flags as detailed below.\n"
        "The additive options are:\n\n";
    QList<VerboseInfo> vil = GetVerboseInfos();
    stable_sort(vil.begin(), vil.end(), lt_vil);
    QList<VerboseInfo>::const_iterator it = vil.begin();
    for (; it != vil.end(); ++it)
    {
        if ((*it).IsAdditive())
            msg += format_help_line(*it);
    }

    msg += "\n";
    msg += "These options can be combined, like so: ";
    msg += " -v upnp,channel,network\n";
    msg += "and they will be added to any previous mask such as 'general'.\n";
    msg += "\n";
    msg += "There are also a few options that clear "
        "any previous verbose masking:\n";
    for (it = vil.begin(); it != vil.end(); ++it)
    {
        if (!(*it).IsAdditive())
            msg += format_help_line(*it);
    }

    msg += "\n";
    msg += "To suppress all but file related messages you could use:"
        "  -v none,file\n";
    msg += "\n";
    msg += "Note: The additive flags can also be used subtractively by\n";
    msg += "prepending 'no'. For example:  -v most,norefcount,nodatabase\n";
    msg += "would print most debugging, but would exclude reference counting\n";
    msg += "and database messages as these can be particularly verbose.\n";

    return msg;
}
