#ifndef NIXQUEUECMDPROCESS_H
#define NIXQUEUECMDPROCESS_H

#include <sys/types.h>
#include <unistd.h>

/**
 * This class guards the resources needed for a running child process.
 */
class NixQueueCmdProcess
{
    pid_t _pid;
    int _pid_fd;
    int _stdin_fd[2];
    int _stdout_fd[2];
    int _stderr_fd[2];

public:
    NixQueueCmdProcess() = delete;
    NixQueueCmdProcess(const pid_t pid);
    ~NixQueueCmdProcess();
};

#endif // NIXQUEUECMDPROCESS_H
