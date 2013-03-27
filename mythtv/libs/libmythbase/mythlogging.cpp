#include <QReadWriteLock>
#include <QAtomicInt>
#include <QString>
#include <QMutex>
#include <QList>
#include <QHash>

#include "mythlogging_extra.h"

MBASE_PUBLIC QAtomicInt g_myth_logging_initialized(0);
/* We initialize the log level and verbose mask so that before
 * logging is initialized all messages are are passed through to
 * log_line.
 */
MBASE_PUBLIC int g_myth_logging_log_level = 0;
MBASE_PUBLIC uint64_t g_myth_logging_verbose_mask = ~(uint64_t(0));

class LogEntry
{
  public:
    LogEntry(uint64_t mask, int32_t level, int32_t file_hash,
             int32_t line, int32_t func_hash, const QString &msg) :
        m_mask(mask), m_funcHash(func_hash), m_fileHash(file_hash),
        m_line(line), m_level(level), m_msg(msg)
    {
    }
  private:
    uint64_t m_mask;
    int32_t  m_funcHash;
    int32_t  m_fileHash;
    int16_t  m_line;
    int16_t  m_level;
    QString  m_msg;
};

class LogDeque
{
  public:
    static LogDeque &GetLogDeque(void)
    {
        return s_logDeque;
    }

    void Append(const LogEntry &logEntry)
    {
        QMutexLocker locker(&m_messagesLock);
        m_messages.push_back(logEntry);
    }

    uint32_t HashString(const char *c_str)
    {
        QByteArray ba(c_str); // TODO eliminate alloc of byte array here
        int32_t hash = qHash(ba);
        {
            QReadLocker read_locker(&m_hashLock);
            if (m_hashMap.contains(hash))
                return hash;
        }
        QWriteLocker write_locker(&m_hashLock);
        m_hashMap[hash] = ba;
        return hash;
    }

    void ProcessQueue(void) {} // stub

    bool IsSingleThreaded(void) const { return true; } // stub

  private:
    LogDeque()
    {
    }

    ~LogDeque()
    {
        ProcessQueue();
    }

    static LogDeque s_logDeque;

    mutable QReadWriteLock m_hashLock;
    QHash<uint32_t, QByteArray> m_hashMap;

    mutable QMutex m_messagesLock;
    QList<LogEntry> m_messages;
};
LogDeque LogDeque::s_logDeque;

MBASE_PUBLIC void log_line(
    uint64_t mask, int level, const char *file, int line,
    const char *function, const QString &msg)
{
    LogDeque &ldq = LogDeque::GetLogDeque();
    ldq.Append(LogEntry(mask, level, ldq.HashString(file), line,
                        ldq.HashString(function), msg));
    if (ldq.IsSingleThreaded())
        ldq.ProcessQueue();
}

MBASE_PUBLIC void log_print(const QString &msg, bool flush)
{
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
    uint64_t default_verbose_mask,
    int default_log_level,
    int default_syslog_facility,
    bool use_threads,
    bool enable_database_logging)
{
}

/// Shuts down logging threads, if there are any.
MBASE_PUBLIC void thread_shutdown(void)
{
}

// TODO syslog??
MBASE_PUBLIC QString command_line_arguments(void)
{
    return QString(" --verbose %1 --logLevel %2 ")
        .arg(format_verbose(g_myth_logging_verbose_mask))
        .arg(format_log_level(g_myth_logging_log_level));
}

MBASE_PUBLIC QString format_verbose(uint64_t mask)
{
    return QString();
}

MBASE_PUBLIC QString format_log_level(int level)
{
    return QString();
}

MBASE_PUBLIC QString format_syslog_facility(int facility)
{
    return QString();
}

MBASE_PUBLIC uint64_t parse_verbose(const QString&, bool *ok)
{
    return 0;
}

MBASE_PUBLIC int parse_log_level(const QString&, bool *ok)
{
    return 0;
}

MBASE_PUBLIC int parse_syslog_facility(const QString &value, bool *ok)
{
    if (value == "none")
        return -2;
    return 0;
}

MBASE_PUBLIC void register_thread(const QString &name)
{
}

MBASE_PUBLIC void deregister_thread(void)
{
}

} // end namespace myth_logging
