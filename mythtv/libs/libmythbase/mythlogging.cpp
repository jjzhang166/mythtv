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

// POSIX headers
#include <errno.h>

// C headers
#include <cstdio> // for vsprintf() on Ubuntu 12.04 (yes it should be in cstdarg).
#include <cstdarg> // for va_list(), va_start(), va_arg(), va_end()

// Qt headers
#include <QStringList>
#include <QDateTime>
#include <QString>
#include <QThread>

// MythTV headers
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
    // TODO optimize the ldq lookups under a single locking/unlocking
    ThreadInfo ti = ldq.LookupThreadInfo(QThread::currentThreadId());
    ldq.LogLine(LogEntry(QDateTime::currentDateTimeUtc(),
                         ti.GetProcessId(), ti.GetThreadId(), ti.GetName(),
                         mask, level, ldq.HashString(file), line,
                         ldq.HashString(function), msg));
}

MBASE_PUBLIC void log_print(const QString &msg, bool flush)
{
    LogDeque::Get().LogLine(LogEntry(msg, flush));
}

MBASE_PUBLIC QString errno_to_qstring(int errnum)
{
    return QString("%1 (%2)").arg(strerror(errnum)).arg(errnum);
}

/// Verbose helper function for ENO macro
MBASE_PUBLIC QString errno_to_qstring(void)
{
    return errno_to_qstring(errno);
}

MBASE_PUBLIC void log_line_c(
    uint64_t mask, unsigned int level, const char *file, int line,
    const char *function, const char *format, ...)
{
    va_list arguments;

    // C adaptation code.
    static const int logLineMaxLen = 4096;
    char buffer[logLineMaxLen];
    va_start(arguments, format);
    vsnprintf(buffer, logLineMaxLen - 1, format, arguments);
    va_end(arguments);
    buffer[logLineMaxLen] = '\0'; // make sure the string is null terminated.

    // Create the QString
    QString msg = QString::fromLocal8Bit(buffer);

    // Call regular C++ log line function
    log_line(mask, level, file, line, function, msg);
}

namespace myth_logging
{
/** \brief Initializes logging.
 *
 *  Before this called all messages sent to logging are saved for
 *  processing after this has been called.
 *
 *  \note This should be called once by
 *        MythCommandLineParser::ConfigureLogging()
 *
 * \param verbose_mask A mask specifying which logging messages should
 *                     be processed, and which should be ignored assuming
 *                     the log level is met. See verbosedefs.h for details.
 * \param log_level    The starting log level, all messages of lesser severity
 *                     are ignored.
 * \param syslog_facility A syslog facility to send logging to.
 * \param use_threads  Use multithreading.
 * \param enabled_database_logging Write logging messages to a SQL database.
 * \param logfile      File to send all logging to (including the logging of
 *                     subprocesses.)
 * \param logprefix    Path+'/'+prefix of where logger should create logging
 *                     files. MythTV subprocesses will place their logging in
 *                     the same directory as well. 
 */
MBASE_PUBLIC void initialize_logging(
    uint64_t verbose_mask,
    int log_level,
    int syslog_facility,
    bool use_threads,
    bool enable_database_logging,
    const QString &logfile,
    const QString &logprefix)
{
    LogDeque::Get().InitializeLogging(
        verbose_mask, log_level, syslog_facility,
        use_threads, enable_database_logging,
        logfile, logprefix);
}

/// Shuts down logging threads, if there are any.
MBASE_PUBLIC void thread_shutdown(void)
{
    LogDeque::Get().ThreadShutdown();
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

/** Formats verbose, log level and log path for another mythtv program.
 */
// TODO log file
// TODO syslog??
MBASE_PUBLIC QString command_line_arguments(void)
{
    uint64_t mask;
    int level;
    bool init;
    LogDeque::Get().GetLogFilter(mask, level, init);
    QString cmd_line = QString(" --verbose %1 --loglevel %2 ")
        .arg(format_verbose(mask))
        .arg(format_log_level(level));

    if (!LogDeque::Get().GetLogPath().isEmpty())
        cmd_line += QString("--logpath %1 ").arg(LogDeque::Get().GetLogPath());

    return cmd_line;
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
            if (name.left(5) == "VB_NO")
            {
                vi = LogDeque::Get().GetVerboseInfo(
                    QString("VB_%1").arg(name.mid(5)));
                if (vi.IsValid() && vi.IsAdditive())
                {
                    add &= ~vi.GetMask();
                    sub |= vi.GetMask();
                    continue;
                }
            }

            ok = false;
            sub = add = 0;
            break;
        }
        if (vi.IsAdditive())
        {
            add |= vi.GetMask();
            sub &= ~vi.GetMask();
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

MBASE_PUBLIC QString register_thread(const QString &name)
{
    return LogDeque::Get().RegisterThread(name);
}

MBASE_PUBLIC QString deregister_thread(void)
{
    return LogDeque::Get().DeregisterThread();
}

MBASE_PUBLIC QString get_verbose_help(void)
{
    return LogDeque::Get().GetVerboseHelp();
}

} // end namespace myth_logging
