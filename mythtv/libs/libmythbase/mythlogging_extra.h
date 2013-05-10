#ifndef MYTHLOGGING_EXTRA_H_
#define MYTHLOGGING_EXTRA_H_

#include "mythlogging.h"

namespace myth_logging {

MBASE_PUBLIC void initialize_logging(
    uint64_t default_verbose_mask,
    int default_log_level,
    int default_syslog_facility,
    bool use_threads,
    bool enable_database_logging,
    const QString &logfile,
    const QString &logprefix);

MBASE_PUBLIC void thread_shutdown(void);

MBASE_PUBLIC int set_log_level(int log_level);
MBASE_PUBLIC int get_log_level(void);

MBASE_PUBLIC uint64_t set_verbose(uint64_t mask);
MBASE_PUBLIC uint64_t get_verbose(void);

MBASE_PUBLIC QString command_line_arguments(void);

MBASE_PUBLIC QString format_verbose(uint64_t mask);
MBASE_PUBLIC QString format_log_level(int level);
MBASE_PUBLIC QString format_syslog_facility(int facility);

MBASE_PUBLIC bool parse_verbose(const QString &, uint64_t &sub, uint64_t &add);
MBASE_PUBLIC bool parse_log_level(const QString&, int &level);
MBASE_PUBLIC bool parse_syslog_facility(const QString&, int &facility);

MBASE_PUBLIC QString register_thread(const QString &name);
MBASE_PUBLIC QString rename_thread(const QString &name);
MBASE_PUBLIC QString deregister_thread(void);

MBASE_PUBLIC QString get_verbose_help(void);

};

#endif // MYTHLOGGING_EXTRA_H_
