#include "mythbaseutil.h"

#include <QtGlobal> // for Q_OS macros, used for system header pulling

// System headers
#if defined(Q_OS_LINUX)
#include <sys/syscall.h>
#elif defined(Q_OS_FREEBSD)
extern "C" {
#include <sys/ucontext.h>
#include <sys/thr.h>
}
#elif defined(Q_OS_MAC)
#include <mach/mach.h>
#endif

// POSIX
#include <sys/types.h>  // for fnctl
#include <fcntl.h>      // for fnctl
#include <errno.h>      // for checking errno
#include <unistd.h>     // for pipe & syscall

// Qt
#include <QString>

// MythTV
#include "mythlogging.h"

/** \brief This sets up a unidirectional pipe.
 *
 *  The pipes are returned in pipefd and the flags on those
 *  pipes are returned in myflags.
 *
 *  At the 0 index is the read end of the pipe and at the 1
 *  index is the write index.
 *
 *  This will attempt to make the read end of the pipe non-blocking.
 *
 *  The pipe ends will be set to -1 if this fails to set up the pipe.
 *  The O_NONBLOCK flag will be missing on myflags[0] if this fails
 *  to make the read pipe non-blocking.
 */
#ifdef USING_MINGW
void setup_pipe(int[2], long[2]) {}
#else // !USING_MINGW
void setup_pipe(int mypipe[2], long myflags[2])
{
    int pipe_ret = pipe(mypipe);
    if (pipe_ret < 0)
    {
        LOG(VB_GENERAL, LOG_ERR, "Failed to open pipes" + ENO);
        mypipe[0] = mypipe[1] = -1;
    }
    else
    {
        errno = 0;
        long flags = fcntl(mypipe[0], F_GETFL);
        if (0 == errno)
        {
            int ret = fcntl(mypipe[0], F_SETFL, flags|O_NONBLOCK);
            if (ret < 0)
                LOG(VB_GENERAL, LOG_ERR, "Set pipe flags error" + ENO);
        }
        else
        {
            LOG(VB_GENERAL, LOG_ERR, "Get pipe flags error" + ENO);
        }

        for (uint i = 0; i < 2; i++)
        {
            errno = 0;
            flags = fcntl(mypipe[i], F_GETFL);
            if (0 == errno)
                myflags[i] = flags;
        }
    }
}
#endif // !USING_MINGW

/// This returns the Thread ID of the current thread in
/// one of the formats reported by the GNU gdb debugger.
MBASE_PUBLIC uint64_t get_gdb_thread_id(void)
{
#if defined(Q_OS_LINUX)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(Q_OS_FREEBSD)
    long lwpid;
    (void) thr_self(&lwpid);
    return static_cast<uint64_t>(lwpid);
#elif defined(Q_OS_MAC)
    return static_cast<uint64_t>(mach_thread_self());
#else
    #warning "get_gdb_thread_id() not implemented on current platform."
    return 0;
#endif
}
