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

#ifndef _LOG_DEQUE_H_
#define _LOG_DEQUE_H_

#include <stdint.h>

#include <QReadWriteLock>
#include <QMutex>
#include <QList>

#include "loglevelinfo.h"
#include "verboseinfo.h"
#include "threadinfo.h"
#include "logentry.h"

class LogHandler;

class LogDeque
{
  public:
    static LogDeque &Get(void)
    {
        return s_logDeque;
    }

    void LogLine(const LogEntry &logEntry)
    {
        QMutexLocker locker(&m_messagesLock);
        m_messages.push_back(logEntry);
        if (IsSingleThreaded())
        {
            locker.unlock();
            ProcessQueue();
        }
    }

    uint32_t HashString(const char *c_str);

    QString LookupHashedString(uint32_t hash) const
    {
        QReadLocker read_locker(&m_hashLock);
        QHash<uint32_t, QString>::const_iterator it = m_hashMap.find(hash);
        if (it != m_hashMap.end())
            return *it;
        return QString();
    }

    void InitializeLogging(
        uint64_t verbose_mask,
        int log_level,
        int syslog_facility,
        bool use_threads,
        bool enable_database_logging,
        const QString &logfile,
        const QString &logprefix);

    bool IsLogged(uint64_t mask, int level) const
    {
        QReadLocker locker(&m_filterLock);
        return ((m_verboseMask & mask) == mask) && (level <= m_logLevel);
    }

    bool IsPossiblyLogged(uint64_t mask, int level) const
    {
        QReadLocker locker(&m_filterLock);
        return (m_verboseMask & mask) && (level <= m_logLevel);
    }

    void GetLogFilter(uint64_t &mask, int &level, bool &init) const
    {
        QReadLocker locker(&m_filterLock);
        mask = m_verboseMask;
        level = m_logLevel;
        init = m_loggingInitialized;
    }

    uint64_t SetVerbose(uint64_t mask)
    {
        QWriteLocker locker(&m_filterLock);
        uint64_t old_val = m_verboseMask;
        m_verboseMask = mask;
        return old_val;
    }

    int SetLogLevel(int log_level)
    {
        QWriteLocker locker(&m_filterLock);
        int old_val = m_logLevel;
        m_logLevel = log_level;
        return old_val;
    }

    QChar LookupLogLevelChar(int level) const
    {
        QHash<int, LogLevelInfo>::const_iterator it =
            m_logLevelInfo.find(level);
        return (it != m_logLevelInfo.end()) ? (*it).GetChar() : QChar('?');
    }

    VerboseInfo GetVerboseInfo(const QString &name) const
    {
        QHash<QString, VerboseInfo>::const_iterator it =
            m_verboseParseInfo.find(name);
        return (it != m_verboseParseInfo.end()) ? (*it) : VerboseInfo();
    }

    LogLevelInfo GetLogLevelInfo(const QString &name) const
    {
        QHash<QString, LogLevelInfo>::const_iterator it =
            m_logLevelParseInfo.find(name);
        return (it != m_logLevelParseInfo.end()) ? (*it) : LogLevelInfo();
    }

    LogLevelInfo GetLogLevelInfo(int level) const
    {
        QHash<int, LogLevelInfo>::const_iterator it =
            m_logLevelInfo.find(level);
        return (it != m_logLevelInfo.end()) ? (*it) : LogLevelInfo();
    }

    ThreadInfo LookupThreadInfo(Qt::HANDLE tid) const
    {
        QReadLocker locker(&m_hashLock);
        QHash<Qt::HANDLE, ThreadInfo>::const_iterator it =
            m_threadInfoMap.find(tid);
        return (it != m_threadInfoMap.end()) ? (*it) : ThreadInfo();
    }

    QString RegisterThread(const QString &name);
    QString DeregisterThread(void);

    QString FormatVerbose(uint64_t mask);
    QString FormatLogLevel(int level)
    {
        return GetLogLevelInfo(level).GetName().mid(4).toLower();
    }

    QString GetVerboseHelp(void) const;

    QString GetLogFile(void) const
    {
        QReadLocker locker(&m_handlerLock);
        return m_logFile;
    }

    QString GetLogPath(void) const
    {
        QReadLocker locker(&m_handlerLock);
        return m_logPath;
    }

  private:
    LogDeque();
    ~LogDeque();

    QList<VerboseInfo> GetVerboseInfos(void) const
    {
        QList<VerboseInfo> verboseInfoList;
        QHash<uint64_t, VerboseInfo>::const_iterator it;
        for (it = m_verboseInfo.begin(); it != m_verboseInfo.end(); ++it)
            verboseInfoList.push_back(*it);
        return verboseInfoList;
    }

    bool IsSingleThreaded(void) const
    {
        return m_singleThreaded;
    }

    void ProcessQueue(bool force = false);

  private:
    static LogDeque s_logDeque;

    // These four maps don't need to live under a lock because
    // they are read-only during the lifetime of the instance.
    QHash<uint64_t, VerboseInfo> m_verboseInfo;
    QHash<QString, VerboseInfo> m_verboseParseInfo;
    QHash<int, LogLevelInfo> m_logLevelInfo;
    QHash<QString, LogLevelInfo> m_logLevelParseInfo;

    mutable QReadWriteLock m_hashLock;
    QHash<uint32_t, QString> m_hashMap; // m_hashLock
    QHash<Qt::HANDLE, ThreadInfo> m_threadInfoMap; // m_hashLock

    mutable QMutex m_messagesLock;
    QList<LogEntry> m_messages; // m_messagesLock

    mutable QReadWriteLock m_filterLock;
    bool m_loggingInitialized; // m_filterLock
    int m_logLevel; // m_filterLock
    uint64_t m_verboseMask; // m_filterLock
    bool m_singleThreaded; // m_filterLock


    mutable QReadWriteLock m_handlerLock;
    QList<LogHandler*> m_handlers; // m_handlerLock
    QString m_logFile; // m_handlerLock
    QString m_logPrefix; // m_handlerLock
    QString m_logPath; // m_handlerLock
};

#endif // _LOG_DEQUE_H_
