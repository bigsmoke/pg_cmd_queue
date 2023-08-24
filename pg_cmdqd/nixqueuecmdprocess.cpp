#include "nixqueuecmdprocess.h"

#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <stdexcept>

#define CMDQD_PIPE_CHUNK_SIZE 512

#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434   /* System call # on most architectures */
#endif

static int
pidfd_open(pid_t pid, unsigned int flags)
{
   return syscall(__NR_pidfd_open, pid, flags);
}


NixQueueCmdProcess::NixQueueCmdProcess(const pid_t pid)
    :_pid(pid)
{
    _pid_fd = pidfd_open(_pid, 0);
    if (_pid_fd == -1)
        throw std::runtime_error(strerror(errno));
}

NixQueueCmdProcess::~NixQueueCmdProcess()
{
}
