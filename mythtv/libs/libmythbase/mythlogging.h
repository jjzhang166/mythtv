#ifndef MYTHLOGGING_H_
#define MYTHLOGGING_H_

#include <stdint.h>
#include <errno.h>

#include "mythbaseexp.h"  //  MBASE_PUBLIC , etc.
#include "verbosedefs.h"

/** MythTV Logging.
 *
 * The functions and variables defined here are not intended to be
 * used directly. They are only intended to be used by the macros
 * and may change at any time.
 *
 * The LOG(MASK, LOG_LEVEL, MESSAGE) macro accepts three parameters:
 *  * The verbose mask
 *  * The log level
 *  * The message
 *
 * LOG_WILL_USE(MASK, LOG_LEVEL)
 * The main reason LOG() is a macro and not a function is that it
 * allows us not to evaluate the message if we know it will not
 * be logged. This means you can use a somewhat complicated
 * expression as the message secure in the knowledge that it
 * won't be evaluated unless needed. There are however times
 * when it is difficult to express the message computation
 * functionally. In that case you can check if the computation
 * of the message will be needed with the LOG_WILL_USE() macro.
 * 
 * The ENO macro returns a QString containg a description of the
 * thread local errno variable. This is only usable in C++ logging.
 * This can be used to print a description of the errors returned
 * from various C API calls.
 *
 * LOG_PRINT() and LOG_PRINT_FLUSH()
 * The LOG() macro's console logging messages are sent to the standard
 * error output so any messages sent to standard output can be
 * disentangled fairly easily via piping. However to ensure the proper
 * interleaving of console output and logging for command line programs
 * these is also a LOG_PRINT() macro. This macro accepts a QString and
 * will send this string to the standard output without interfering
 * with any LOG() output. LOG_PRINT_FLUSH() is the same except it
 * also flushes the standard output pipe so any message is displayed
 * immediately.
 *
 * Additional logging functionality is exposed in the mythlogging_extra.h
 * header, and resides in the myth_logging namespace.
 *
 */


/// Old habits die hard, reminder to use LOG() instead of VERBOSE().
#define VERBOSE please_use_LOG_instead_of_VERBOSE

/// Helper for checking verbose mask & level
#define LOG_WILL_USE(_MASK_, _LEVEL_) log_will_use(_MASK_, _LEVEL_)
/// Helper for checking verbose mask & level
#define LOG_MIGHT_USE(_MASK_, _LEVEL_) log_might_use(_MASK_, _LEVEL_)

#ifdef __cplusplus
#define LOG(_MASK_, _LEVEL_, _STRING_)                                  \
    do {                                                                \
        if (log_will_use(_MASK_, _LEVEL_))                              \
        {                                                               \
            log_line(_MASK_, _LEVEL_, __FILE__, __LINE__, __FUNCTION__, \
                     _STRING_);                                         \
        }                                                               \
    } while (false)

#define LOG_PRINT(_STRING_) \
    do { log_print(_STRING_, false); } while (false)
#define LOG_PRINT_FLUSH(_STRING_) \
    do { log_print(_STRING_, true); } while (false)

#define ENO (QString("\neno: ") + errno_to_qstring(errno))

class QString;

MBASE_PUBLIC void log_line(
    uint64_t mask, int level, const char *file, int line,
    const char *function, const QString &msg);

MBASE_PUBLIC void log_print(const QString &msg, bool flush);

/// Verbose helper function for ENO macro
MBASE_PUBLIC QString errno_to_qstring(int errnum);

#endif

#ifndef __cplusplus
#define LOG(_MASK_, _LEVEL_, _FORMAT_, ...)                             \
    do {                                                                \
        if (log_will_use(_MASK_, _LEVEL_))                              \
        {                                                               \
            log_line_c(_MASK_, _LEVEL_,                                 \
                       __FILE__, __LINE__, __FUNCTION__, 0,             \
                       (const char *)_FORMAT_, ##__VA_ARGS__);          \
        }                                                               \
    } while (0)
#endif // end of C section

#ifdef __cplusplus
extern "C" MBASE_PUBLIC 
#endif
void log_line_c(
    uint64_t mask, unsigned int level, const char *file, int line, 
    const char *function, const char *format, ...);

/// Please don't use this directly, use LOG_WILL_USE()
#ifdef __cplusplus
extern "C"
#endif
MBASE_PUBLIC int log_will_use(uint64_t mask, int level);

/// Please don't use this directly, use LOG_MIGHT_USE()
#ifdef __cplusplus
extern "C"
#endif
MBASE_PUBLIC int log_might_use(uint64_t mask, int level);

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
