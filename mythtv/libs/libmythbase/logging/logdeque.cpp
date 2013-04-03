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

#include <algorithm> // for stable_sort
#include <iostream>
using namespace std;

#include <QStringList>
#include <QThread>

#include "logdeque.h"

LogDeque LogDeque::s_logDeque;

uint32_t LogDeque::HashString(const char *c_str)
{
    // TODO eliminate alloc of byte array here
    QByteArray ba(c_str);
    int32_t hash = qHash(ba);
    {
        QReadLocker read_locker(&m_hashLock);
        if (m_hashMap.contains(hash))
            return hash;
    }
    QWriteLocker write_locker(&m_hashLock);
    m_hashMap[hash] = QString::fromUtf8(ba);
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

        QList<LogEntry>::const_iterator it = first_few.begin();
        for (; it != first_few.end(); ++it)
        {
            if (((verboseMask & (*it).GetMask()) == (*it).GetMask()) &&
                (logLevel <= (*it).GetLevel()))
            {
                cerr << qPrintable((*it).toString()) << endl;
            }
            else if ((*it).IsPrint())
            {
                cerr << qPrintable((*it).GetMessage()) << endl;
            }
        }
    }
    while (!first_few.empty());
}

LogDeque::LogDeque() :
    m_loggingInitialized(false),
    m_logLevel(INT_MAX),
    m_verboseMask(~(uint64_t(0)))
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
#define LOGLEVEL_MAP(NAME, VALUE, CHAR_NAME)                      \
    m_logLevelParseInfo[QString(#NAME)] = m_logLevelInfo[VALUE] = \
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

QString LogDeque::RegisterThread(const QString &name)
{
    Qt::HANDLE handle = QThread::currentThreadId();
    int process_id = 0; // TODO lookup

    ThreadInfo ti(name, handle, process_id);

    QWriteLocker locker(&m_hashLock);
    ThreadInfo old = m_threadInfoMap[handle];
    m_threadInfoMap[handle] = ti;
    return old.GetName();
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
