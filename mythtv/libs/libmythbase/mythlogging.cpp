/*
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

#include <QStringList>
#include <QDateTime>
#include <QString>
#include <QThread>

#include "mythlogging_extra.h"
#include "logging/logentry.h"
#include "logging/verboseinfo.h"
#include "logging/loglevelinfo.h"
#include "logging/threadinfo.h"
#include "logging/logdeque.h"

MBASE_PUBLIC int log_will_use(uint64_t mask, int level)
{
    return LogDeque::Get().IsLogged(mask, level);
}

MBASE_PUBLIC int log_might_use(uint64_t mask, int level)
{
    return LogDeque::Get().IsPossiblyLogged(mask, level);
}

MBASE_PUBLIC void log_line(
    uint64_t mask, int level, const char *file, int line,
    const char *function, const QString &msg)
{
    LogDeque &ldq = LogDeque::Get();
    // TODO optimize the ldw lookups under a single locking/unlocking
    ThreadInfo ti = ldq.LookupThreadInfo(QThread::currentThreadId());
    ldq.Append(LogEntry(QDateTime::currentDateTimeUtc(),
                        ti.GetProcessId(), ti.GetThreadId(), ti.GetName(),
                        mask, level, ldq.HashString(file), line,
                        ldq.HashString(function), msg));
    if (ldq.IsSingleThreaded())
        ldq.ProcessQueue();
}

MBASE_PUBLIC void log_print(const QString &msg, bool flush)
{
    // TODO
}

/// Verbose helper function for ENO macro
MBASE_PUBLIC QString errno_to_qstring(int errnum)
{
    return QString();
}

MBASE_PUBLIC void log_line_c(
    uint64_t mask, unsigned int level, const char *file, int line,
    const char *function, const char *format, ...)
{
}

namespace myth_logging
{
/// This should be called once by MythCommandLineParser::ConfigureLogging()
MBASE_PUBLIC void set_parameters(
    uint64_t verbose_mask,
    int log_level,
    int syslog_facility,
    bool use_threads,
    bool enable_database_logging)
{
    LogDeque::Get().InitializeLogging(
        verbose_mask, log_level, syslog_facility,
        use_threads, enable_database_logging);
}

/// Shuts down logging threads, if there are any.
MBASE_PUBLIC void thread_shutdown(void)
{
}

MBASE_PUBLIC int set_log_level(int log_level)
{
    return LogDeque::Get().SetLogLevel(log_level);
}

MBASE_PUBLIC int get_log_level(void)
{
    uint64_t mask;
    int level;
    bool init;
    LogDeque::Get().GetLogFilter(mask, level, init);
    return level;
}

MBASE_PUBLIC uint64_t set_verbose(uint64_t mask)
{
    return LogDeque::Get().SetVerbose(mask);
}

MBASE_PUBLIC uint64_t get_verbose(void)
{
    uint64_t mask;
    int level;
    bool init;
    LogDeque::Get().GetLogFilter(mask, level, init);
    return mask;
}

/// formats verbose, log level, and syslog facility for another mythtv program
// TODO syslog??
MBASE_PUBLIC QString command_line_arguments(void)
{
    uint64_t mask;
    int level;
    bool init;
    LogDeque::Get().GetLogFilter(mask, level, init);
    return QString(" --verbose %1 --loglevel %2 ")
        .arg(format_verbose(mask))
        .arg(format_log_level(level));
}

MBASE_PUBLIC QString format_verbose(uint64_t mask)
{
    return LogDeque::Get().FormatVerbose(mask);
}

MBASE_PUBLIC QString format_log_level(int level)
{
    return LogDeque::Get().FormatLogLevel(level);
}

MBASE_PUBLIC QString format_syslog_facility(int facility)
{
    return QString();
}

MBASE_PUBLIC bool parse_verbose(
    const QString &verboseStr,
    uint64_t &subtractiveMask, uint64_t &additiveMask)
{
    bool ok = true;

    uint64_t sub = 0;
    uint64_t add = 0;

    QStringList l = verboseStr.split(",");
    for (QStringList::iterator it = l.begin(); it != l.end(); ++it)
    {
        QString name = QString("VB_%1").arg((*it).trimmed().toUpper());
        VerboseInfo vi = LogDeque::Get().GetVerboseInfo(name);
        if (!vi.IsValid())
        {
            ok = false;
            sub = add = 0;
            break;
        }
        if (vi.IsAdditive())
        {
            add |= vi.GetMask();
        }
        else
        {
            sub = ~0x0ULL;
            add = vi.GetMask();
        }
    }

    if (ok)
    {
        subtractiveMask = sub;
        additiveMask = add;
    }

    return ok;
}

MBASE_PUBLIC bool parse_log_level(const QString &levelStr, int &logLevel)
{
    // TODO FIXME This will parse 'any' and 'unknown'
    QString name = QString("LOG_%1").arg(levelStr.trimmed().toUpper());
    LogLevelInfo lli = LogDeque::Get().GetLogLevelInfo(name);
    if (lli.IsValid())
    {
        logLevel = lli.GetLevel();
        return true;
    }
    return false;
}

MBASE_PUBLIC bool parse_syslog_facility(const QString &value, int &facility)
{
    return false;
}

MBASE_PUBLIC void register_thread(const QString &name)
{
    LogDeque::Get().RegisterThread(name);
}

MBASE_PUBLIC void deregister_thread(void)
{
    LogDeque::Get().DeregisterThread();
}

} // end namespace myth_logging
