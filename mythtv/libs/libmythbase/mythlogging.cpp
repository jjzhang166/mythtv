#include <iostream>
using namespace std;

#include <QReadWriteLock>
#include <QStringList>
#include <QAtomicInt>
#include <QDateTime>
#include <QString>
#include <QThread>
#include <QMutex>
#include <QList>
#include <QHash>

#define VERBOSEDEFS_H_
#include "mythlogging_extra.h"

static QString lookup_hashed_string(uint32_t);
static QChar lookup_log_level_char(int level);

class LogEntry
{
  public:
    /**
     * \note We hash file & function strings because they never change
     * \note We keep the thread_name as a QString because some threads
     *       change their names. Notably QRunnables in the threadpool
     *       change the name of the worker thread and the main or UI
     *       thread becomes the CoreContext thread for much of it's
     *       lifetime.
     */
    LogEntry(const QDateTime &when,
             uint64_t process_id, Qt::HANDLE thread_id,
             const QString &thread_name,
             uint64_t mask, int32_t level, int32_t file_hash,
             int32_t line, int32_t func_hash,
             const QString &msg) :
        m_when(when), m_mask(mask),
        m_processId(process_id), m_threadId(thread_id),
        m_funcHash(func_hash), m_fileHash(file_hash),
        m_line(line), m_level(level),
        m_msg(msg), m_threadName(thread_name)
    {
    }

    /** Returns a string with the following format:
     * 2013-03-24 11:22:11.284064 N [pid/tid] ThreadName filename.cpp:line (FunctionName) - msg
     */
    QString toString() const
    {
        return QString("%1 %2 [%3/%4] %5 %6:%7 %8 - %9")
            .arg(m_when.toString("yyyy-MM-dd hh:mm:ss.zzz"))
            .arg(lookup_log_level_char(m_level))
            .arg(0/*m_processId*/)
            .arg(0/*reinterpret_cast<uint64_t>(m_threadId)*/)
            .arg(m_threadName)
            .arg(lookup_hashed_string(m_fileHash))
            .arg(m_line)
            .arg(lookup_hashed_string(m_funcHash))
            .arg(m_msg);
    }

    uint64_t GetMask(void) const { return m_mask; }
    int GetLevel(void) const { return m_level; }

  private:
    QDateTime m_when;
    uint64_t m_mask;
    uint64_t m_processId;
    Qt::HANDLE m_threadId;
    int32_t m_funcHash;
    int32_t m_fileHash;
    int16_t m_line;
    int16_t m_level;
    QString m_msg;
    QString m_threadName;
};

class VerboseInfo
{
  public:
    VerboseInfo() : 
        m_mask(0), m_name(QString::null), m_additive(false), m_help()
    {
    }

    VerboseInfo(uint64_t mask, const QString &name,
                bool additive, const QString &help) :
        m_mask(mask), m_name(name), m_additive(additive), m_help(help)
    {
    }

    bool IsValid(void) const { return !m_name.isNull(); }

    uint64_t GetMask(void) const { return m_mask; }
    bool IsAdditive(void) const { return m_additive; }
    QString GetHelp(void) const { return m_help; }

    QString toString() const
    {
        return QString("mask(0x%1) name(%2) additive(%3) help(%4)")
            .arg(m_mask, 16, 16, QChar('0'))
            .arg(m_name).arg(m_additive).arg(m_help);
    }

  private:
    uint64_t m_mask;
    QString m_name;
    bool m_additive;
    QString m_help;
};

class LogLevelInfo
{
  public:
    LogLevelInfo() : m_value(0), m_name(QString::null)
    {
    }

    LogLevelInfo(int value, const QString &name, QChar charName) :
        m_value(value), m_name(name), m_charName(charName)
    {
    }

    bool IsValid(void) const { return !m_name.isNull(); }

    int GetLevel(void) const { return m_value; }
    QString GetName(void) const { return m_name; }
    QChar GetChar(void) const { return m_charName; }

    QString toString() const
    {
        return QString("level(%1) char_name(%2) name(%3)")
            .arg(m_value).arg(m_charName).arg(m_name);
    }

  private:
    int m_value;
    QString m_name;
    QChar m_charName;
};

class ThreadInfo
{
  public:
    ThreadInfo() : m_name("Unknown"), m_processId(0)
    {
    }

    ThreadInfo(QString name, Qt::HANDLE handle, uint64_t processId) :
        m_name(name), m_handle(handle), m_processId(processId)
    {
    }

    QString GetName(void) const { return m_name; }
    Qt::HANDLE GetThreadId(void) const { return m_handle; }
    uint64_t GetProcessId(void) const { return m_processId; }

  private:
    QString m_name;
    Qt::HANDLE m_handle;
    uint64_t m_processId;
};

class LogDeque
{
  public:
    static LogDeque &Get(void)
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

    QString LookupHashedString(uint32_t hash) const
    {
        QReadLocker read_locker(&m_hashLock);
        QHash<uint32_t, QString>::const_iterator it = m_hashMap.find(hash);
        if (it != m_hashMap.end())
            return *it;
        return QString();
    }

    void ProcessQueue(bool force = false)
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
            }
        }
        while (!first_few.empty());
    }

    bool IsSingleThreaded(void) const
    {
        // TODO implement properly
        return true;
    }

    void InitializeLogging(
        uint64_t verbose_mask,
        int log_level,
        int syslog_facility,
        bool use_threads,
        bool enable_database_logging)
    {
        QWriteLocker locker(&m_filterLock);
        m_loggingInitialized = true;
        m_verboseMask = verbose_mask;
        m_logLevel = log_level;
        /* use_threads */
        /* enable_database_logging */
    }

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

    ThreadInfo LookupThreadInfo(Qt::HANDLE tid) const
    {
        QReadLocker locker(&m_hashLock);
        QHash<Qt::HANDLE, ThreadInfo>::const_iterator it =
            m_threadInfoMap.find(tid);
        return (it != m_threadInfoMap.end()) ? (*it) : ThreadInfo();
    }

    void RegisterThread(const QString &name)
    {
        Qt::HANDLE handle = QThread::currentThreadId();
        int process_id = 0; // TODO lookup

        ThreadInfo ti(name, handle, process_id);

        QWriteLocker locker(&m_hashLock);
        m_threadInfoMap[handle] = ti;
    }

    void DeregisterThread(void)
    {
        QWriteLocker locker(&m_hashLock);
        QHash<Qt::HANDLE, ThreadInfo>::iterator it =
            m_threadInfoMap.find(QThread::currentThreadId());
        if (it != m_threadInfoMap.end())
            m_threadInfoMap.erase(it);
    }

  private:
    LogDeque() :
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
#define VERBOSE_MAP(NAME, MASK, ADDITIVE, HELP) \
    m_verboseParseInfo[QString(#NAME)] = m_verboseInfo[MASK] = \
        VerboseInfo(MASK, QString(#NAME), ADDITIVE, QString(HELP));
#define LOGLEVEL_MAP(NAME, VALUE, CHAR_NAME) \
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

    ~LogDeque()
    {
        ProcessQueue(/*force*/ true);
    }

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
};
LogDeque LogDeque::s_logDeque;

static QString lookup_hashed_string(uint32_t key)
{
    return LogDeque::Get().LookupHashedString(key);
}

static QChar lookup_log_level_char(int level)
{
    return LogDeque::Get().LookupLogLevelChar(level);
}

int log_will_use(uint64_t mask, int level)
{
    return LogDeque::Get().IsLogged(mask, level);
}

int log_might_use(uint64_t mask, int level)
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

// TODO syslog??
MBASE_PUBLIC QString command_line_arguments(void)
{
    uint64_t mask;
    int level;
    bool init;
    LogDeque::Get().GetLogFilter(mask, level, init);
    return QString(" --verbose %1 --logLevel %2 ")
        .arg(format_verbose(mask))
        .arg(format_log_level(level));
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
