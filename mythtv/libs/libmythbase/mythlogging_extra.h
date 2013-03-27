#ifndef MYTHLOGGING_EXTRA_H_
#define MYTHLOGGING_EXTRA_H_

#include "mythlogging.h"

namespace myth_logging {

MBASE_PUBLIC void set_parameters(
    uint64_t default_verbose_mask,
    int default_log_level,
    int default_syslog_facility,
    bool use_threads,
    bool enable_database_logging);

MBASE_PUBLIC void thread_shutdown(void);

inline int get_log_level(void) { return g_myth_logging_log_level; }
inline uint64_t get_verbose(void) { return g_myth_logging_verbose_mask; }

inline int set_log_level(int log_level)
{
    int old_val = g_myth_logging_log_level;
    g_myth_logging_log_level = log_level;
    return old_val;
}

inline uint64_t set_verbose(uint64_t mask)
{
    uint64_t old_val = g_myth_logging_verbose_mask;
    g_myth_logging_verbose_mask = mask;
    return old_val;
}

/// formats verbose, log level, and syslog facility for anther mythtv program
MBASE_PUBLIC QString command_line_arguments(void);

MBASE_PUBLIC QString format_verbose(uint64_t mask);
MBASE_PUBLIC QString format_log_level(int level);
MBASE_PUBLIC QString format_syslog_facility(int facility);

MBASE_PUBLIC uint64_t parse_verbose(const QString&, bool *ok=NULL);
MBASE_PUBLIC int parse_log_level(const QString&, bool *ok=NULL);
MBASE_PUBLIC int parse_syslog_facility(const QString&, bool *ok=NULL);

MBASE_PUBLIC void register_thread(const QString &name);
MBASE_PUBLIC void deregister_thread(void);
};

#endif // MYTHLOGGING_EXTRA_H_
