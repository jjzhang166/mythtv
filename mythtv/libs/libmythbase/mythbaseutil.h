#ifndef _MYTH_DB_UTIL_H_
#define _MYTH_DB_UTIL_H_

#include <stdint.h> // for uint64_t

#include "mythbaseexp.h"

MBASE_PUBLIC void setup_pipe(int pipefd[2], long myflags[2]);
MBASE_PUBLIC uint64_t get_gdb_thread_id(void);

#endif // _MYTH_DB_UTIL_H_
