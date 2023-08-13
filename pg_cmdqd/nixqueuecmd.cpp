#include "nixqueuecmd.h"

#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <memory>

#include "argv_guard.h"
#include "envp_guard.h"
#include "lwpg_result.h"
#include "lwpg_array.h"
#include "lwpg_hstore.h"
#include "utils.h"

#define PGCMDQ_PIPE_BUFFER_SIZE 512

NixQueueCmd::NixQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept
{
    try
    {
        queue_cmd_class = PQgetvalue(result->get(), row, fieldMapping.at("queue_cmd_class"));

        queue_cmd_relname = PQgetvalue(result->get(), row, fieldMapping.at("queue_cmd_relname"));

        cmd_id = PQgetvalue(result->get(), row, fieldMapping.at("cmd_id"));

        cmd_subid = lwpg::getnullable(result->get(), row, fieldMapping.at("cmd_subid"));

        if (PQgetisnull(result->get(), row, fieldMapping.at("cmd_argv")))
        {
            throw std::domain_error("`cmd_argv` should never be `NULL`.");
        }
        std::string raw_cmd_argv = PQgetvalue(result->get(), row, fieldMapping.at("cmd_argv"));
        cmd_argv = lwpg::array_to_vector(raw_cmd_argv);

        if (PQgetisnull(result->get(), row, fieldMapping.at("cmd_env")))
        {
            throw std::domain_error("`cmd_env` should never be `NULL`.");
        }
        std::string raw_cmd_env = PQgetvalue(result->get(), row, fieldMapping.at("cmd_env"));
        cmd_env = lwpg::hstore_to_unordered_map(raw_cmd_env);

        // For `cmd_stdin`, we can ignore NULLness, because `PGgetvalue()` returns an empty string when the
        // field is `NULL`, which is what we'd want anyway.
        cmd_stdin = PQgetvalue(result->get(), row, fieldMapping.at("cmd_stdin"));

        _is_valid = true;
    }
    catch (std::exception &ex)
    {
        std::cerr << "Error parsing user data for '" << this->queue_cmd_class.c_str() << "': " << ex.what();
        _is_valid = false;
    }
}

bool NixQueueCmd::is_valid() const
{
    return _is_valid;
}

void NixQueueCmd::run_cmd()
{
    this->cmd_runtime_start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / 1000;

    // TODO: Ask Wiebe: Why catch signals _before_ doing the fork?
    /*
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }
    */

    // TODO: fdguard jatten van https://github.com/victronenergy/dbus-flashmq/blob/master/src/utils.cpp#L125
    int stdin_fd[2], stdout_fd[2], stderr_fd[2];

    if (pipe(stdin_fd) == -1)
        throw std::runtime_error(strerror(errno));
    if (pipe(stdout_fd) == -1)
        throw std::runtime_error(strerror(errno));
    if (pipe(stderr_fd) == -1)
        throw std::runtime_error(strerror(errno));

    pid_t pid = fork();
    if (pid == -1)
    {
        throw std::runtime_error(strerror(errno));
    }

    if (pid == 0)
    {
        close(stdin_fd[1]);  // Close unused write end of STDIN. */
        close(stdout_fd[0]);  // Close unused read end of STDOUT. */
        close(stderr_fd[0]);  // Close unused read end of STDERR. */
        while ((dup2(stdin_fd[0], STDIN_FILENO) == -1) && (errno == EINTR)) {}
        while ((dup2(stdout_fd[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
        while ((dup2(stderr_fd[1], STDERR_FILENO) == -1) && (errno == EINTR)) {}

        // Brute force close any open file/socket, because I don't want the child to see them.
        struct rlimit rlim;
        memset(&rlim, 0, sizeof (struct rlimit));
        getrlimit(RLIMIT_NOFILE, &rlim);
        for (rlim_t i = 0; i < rlim.rlim_cur; ++i) close (i);

        argv_guard argv(this->cmd_argv);
        envp_guard envp(this->cmd_env);

        execvpe(this->cmd_argv[0].c_str(), argv.get(), envp.get());

        // We only get here if the call to `execvpe()` fails.
        std::cerr << strerror(errno) << std::endl;
        exit(127);  // Same as when bash can't find a command.
    }
    else
    {
        printf("Child is PID %jd\n", (intmax_t) pid);

        // TODO:
        //sigaction(SIGCHLD,

        close(stdin_fd[0]);  // Close unused read end of STDIN. */
        close(stdout_fd[1]);  // Close unused write end of STDOUT. */
        close(stderr_fd[1]);  // Close unused write end of STDERR. */

        struct pollfd fds[] = {
            { stdin_fd[1], POLLOUT | POLLERR, 0 },
            { stdout_fd[0], POLLIN | POLLERR, 0 },
            { stderr_fd[0], POLLIN | POLLERR, 0 }
        };

        char stdout_buf[PGCMDQ_PIPE_BUFFER_SIZE];
        char stderr_buf[PGCMDQ_PIPE_BUFFER_SIZE];
        ssize_t stdout_bytes_read = 0;  // non-cumulative
        ssize_t stderr_bytes_read = 0;  // non-cumulative
        ssize_t stdin_bytes_written = 0;  // cumulative

        // TODO: ignore interrupts & handle errors (close pipes, read exit code, etc)
        int fd_count;
        while (true)
        {
            fd_count = poll(fds, 3, 1000);
            if (fd_count < 0)
            {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error(strerror(errno));
            }
            if (fd_count > 0)
            {
                if (fds[0].revents & POLLOUT)
                {
                    if (fds[0].revents & (POLLERR | POLLHUP))
                    {
                        throw std::runtime_error(strerror(errno));
                    }

                    while (stdin_bytes_written < (ssize_t)cmd_stdin.length())
                    {
                        stdin_bytes_written += write(
                            stdin_fd[1],
                            this->cmd_stdin.c_str()+stdin_bytes_written,
                            std::min<int>(this->cmd_stdin.length()-stdin_bytes_written, PGCMDQ_PIPE_BUFFER_SIZE)
                        );
                    }
                    // TODO: What happens on close([0-2]) in the child?
                }
                if (fds[1].revents & POLLIN)
                {
                    if (fds[1].revents & (POLLERR | POLLHUP))
                    {
                        throw std::runtime_error(strerror(errno));
                    }

                    // TODO: Moeten die `std:string`s eerst nog leeg geinitialiseerd worden?
                    while ((stdout_bytes_read = read(stdout_fd[0], &stdout_buf, PGCMDQ_PIPE_BUFFER_SIZE)) > 0)
                        this->cmd_stdout += std::string(stdout_buf, stdout_bytes_read);
                }
                if (fds[2].revents & POLLIN)
                {
                    if (fds[1].revents & (POLLERR | POLLHUP))
                    {
                        throw std::runtime_error(strerror(errno));
                    }

                    while ((stderr_bytes_read = read(stderr_fd[0], &stderr_buf, PGCMDQ_PIPE_BUFFER_SIZE)) > 0)
                        this->cmd_stderr += std::string(stderr_buf, stderr_bytes_read);
                }
            }

            int wstatus;
            int res_pid = waitpid(pid, &wstatus, WNOHANG);

            if (res_pid < 0)
            {
                this->cmd_term_sig = -1;
                this->cmd_stderr += formatString(
                    "Unexpected error while calling `waitpid(%i, &wstatus, WNOHANG)`: ''",
                    pid, strerror(errno)
                );
            }
            else if (res_pid > 0)
            {
                if (not WIFEXITED(wstatus))
                {
                    if (WIFSIGNALED(wstatus))
                    {
                        this->cmd_term_sig = WTERMSIG(wstatus);
                        this->cmd_stderr += formatString(
                            "Process %i terminated with signal %i - %s",
                            pid, this->cmd_term_sig.value(), strsignal(this->cmd_term_sig.value())
                        );
                    }
                    else
                    {
                        // The child process exited abnormally and we couldn't even retrieve a terminating signal.
                        this->cmd_term_sig = -2;
                        // After `UPDATE`, either `cmd_exit_code` or `cmd_term_sig` has to be `NOT NULL`.
                    }
                }
                else
                {
                    this->cmd_exit_code = WEXITSTATUS(wstatus);
                }
                break;
            }
        }
    }

    this->cmd_runtime_end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / 1000;
}
