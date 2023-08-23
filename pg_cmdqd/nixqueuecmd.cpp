#include "nixqueuecmd.h"

#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <numeric>
#include <iostream>
#include <memory>

#include "cmdqueue.h"
#include "lwpg_result.h"
#include "lwpg_array.h"
#include "lwpg_hstore.h"
#include "utils.h"

#define CMDQD_PIPE_BUFFER_SIZE 512

#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434   /* System call # on most architectures */
#endif

static int
pidfd_open(pid_t pid, unsigned int flags)
{
   return syscall(__NR_pidfd_open, pid, flags);
}

const std::string NixQueueCmd::SELECT_STMT_WITHOUT_RELNAME = R"SQL(
    SELECT
        queue_cmd_class::text as queue_cmd_class
        ,(parse_ident(queue_cmd_class::text))[
            array_upper(parse_ident(queue_cmd_class::text), 1)
        ] as queue_cmd_relname
        ,cmd_id
        ,cmd_subid
        ,extract(epoch from cmd_queued_since) as cmd_queued_since
        ,cmd_argv
        ,cmd_env
        ,cmd_stdin
    FROM
        cmdq.%s
    ORDER BY
        cmd_queued_since
    LIMIT 1
    FOR UPDATE SKIP LOCKED
)SQL";

const std::string NixQueueCmd::UPDATE_STMT_WITHOUT_RELNAME = R"SQL(
    UPDATE
        cmdq.%s
    SET
        cmd_runtime = tstzrange(to_timestamp($3), to_timestamp($4))
        ,cmd_exit_code = $5
        ,cmd_term_sig = $6
        ,cmd_stdout = $7
        ,cmd_stderr = $8
    WHERE
        cmd_id = $1
        AND cmd_subid IS NOT DISTINCT from $2
)SQL";


std::string NixQueueCmd::select_stmt(const CmdQueue &cmd_queue)
{
    return formatString(SELECT_STMT_WITHOUT_RELNAME, cmd_queue.queue_cmd_relname.c_str());
}

std::string NixQueueCmd::update_stmt(const CmdQueue &cmd_queue) const
{
    return formatString(UPDATE_STMT_WITHOUT_RELNAME, cmd_queue.queue_cmd_relname.c_str());
}

std::vector<std::optional<std::string>> NixQueueCmd::update_params() const
{
    std::vector<std::optional<std::string>> params;
    params.reserve(8);

    params.push_back(cmd_id);
    params.push_back(cmd_subid);
    params.push_back(formatString("%f", cmd_runtime_start));
    params.push_back(formatString("%f", cmd_runtime_end));
    params.push_back(lwpg::to_nullable_string(cmd_exit_code));
    params.push_back(lwpg::to_nullable_string(cmd_term_sig));
    params.push_back(cmd_stdout);
    params.push_back(cmd_stderr);

    return params;
}

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
        logger->log(LOG_ERROR, "Error parsing user data for '%s': %s", this->queue_cmd_class.c_str(), ex.what());
        _is_valid = false;
    }
}

bool NixQueueCmd::is_valid() const
{
    return _is_valid;
}

std::string NixQueueCmd::cmd_line() const
{
    std::string line;
    // TODO: Use stringstream?

    for (size_t i = 0; i < this->cmd_argv.size(); i++)
    {
        if (i > 0)
            line += " ";
        // TODO: quoting if necessary
        line += this->cmd_argv[i];
    }

    return line;
}

bool NixQueueCmd::cmd_succeeded() const
{
    return cmd_exit_code.has_value() and cmd_exit_code.value() == 0;
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
        for (rlim_t i = 3; i < rlim.rlim_cur; ++i) close (i);

        std::vector<char *> argv_heads;
        argv_heads.reserve(this->cmd_argv.size() + 1);
        for (const std::string &s : cmd_argv)
            argv_heads.push_back(const_cast<char*>(s.c_str()));
        argv_heads[argv_heads.size()-1] = nullptr;
        char **c_argv = argv_heads.data();
        // We don't have to worry about cleaning up c_argv, because execvpe will clear up all that.

        std::vector<std::string> env_flat;
        env_flat.reserve(this->cmd_env.size());
        for (const std::pair<const std::string, std::string> &var : this->cmd_env)
            env_flat.push_back(var.first + "=" + var.second);
        std::vector<char *> envp_heads;
        envp_heads.reserve(this->cmd_env.size() + 1);
        for (const std::string &s : env_flat)
            envp_heads.push_back(const_cast<char*>(s.c_str()));
        envp_heads[envp_heads.size()-1] = nullptr;
        char **c_envp = envp_heads.data();
        // We don't have to worry about cleaning up c_envp, because execvpe will clear up all that.

        execvpe(this->cmd_argv[0].c_str(), c_argv, c_envp);

        // We only get here if the call to `execvpe()` fails.
        std::cerr << strerror(errno) << std::endl;
        exit(127);  // Same as when bash can't find a command.
    }
    else
    {
        logger->log(LOG_DEBUG2, "fork() child PID = %jd\n", (intmax_t) pid);

        int pid_fd = pidfd_open(pid, 0);
        if (pid_fd == -1)
            throw std::runtime_error(strerror(errno));

        close(stdin_fd[0]);  // Close unused read end of STDIN. */
        close(stdout_fd[1]);  // Close unused write end of STDOUT. */
        close(stderr_fd[1]);  // Close unused write end of STDERR. */

        struct pollfd fds[] = {
            { stdin_fd[1], POLLOUT | POLLHUP | POLLERR, 0 },
            { stdout_fd[0], POLLIN | POLLHUP | POLLERR, 0 },
            { stderr_fd[0], POLLIN | POLLHUP | POLLERR, 0 },
            { pid_fd, POLLIN | POLLERR, 0 },
        };

        char stdout_buf[CMDQD_PIPE_BUFFER_SIZE];
        char stderr_buf[CMDQD_PIPE_BUFFER_SIZE];
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
                if ((not cmd_stdin.empty()) and fds[0].revents & POLLOUT)
                {
                    logger->log(LOG_DEBUG5, "Pre");
                    while (stdin_bytes_written < (ssize_t)cmd_stdin.length())
                    {
                        stdin_bytes_written += write(
                            stdin_fd[1],
                            this->cmd_stdin.c_str()+stdin_bytes_written,
                            std::min<int>(this->cmd_stdin.length()-stdin_bytes_written, CMDQD_PIPE_BUFFER_SIZE)
                        );
                    }
                    logger->log(LOG_DEBUG5, "Post");
                }
                if (fds[1].revents & POLLIN)
                {
                    while ((stdout_bytes_read = read(stdout_fd[0], &stdout_buf, CMDQD_PIPE_BUFFER_SIZE)) > 0)
                        this->cmd_stdout += std::string(stdout_buf, stdout_bytes_read);
                }
                if (fds[2].revents & POLLIN)
                {
                    while ((stderr_bytes_read = read(stderr_fd[0], &stderr_buf, CMDQD_PIPE_BUFFER_SIZE)) > 0)
                        this->cmd_stderr += std::string(stderr_buf, stderr_bytes_read);
                }

                if (fds[0].revents & POLLHUP)
                {
                    logger->log(LOG_DEBUG5, "Child %i closed STDIN", pid);
                    close(fds[0].fd);
                }
                if (fds[1].revents & POLLHUP)
                {
                    logger->log(LOG_DEBUG5, "Child %i closed STDOUT", pid);
                    close(fds[1].fd);
                    break;
                }
                if (fds[2].revents & POLLHUP)
                {
                    logger->log(LOG_DEBUG5, "Child %i closed STDERR", pid);
                    close(fds[2].fd);
                    break;
                }

                if (fds[3].revents & POLLIN)
                {
                    logger->log(LOG_DEBUG5, "Child %i ended", pid);
                    close(fds[3].fd);
                    break;
                }
            } // if (fd_count > 0)
        } // while (true)

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
                    /*
                    this->cmd_stderr += formatString(
                        "Process %i terminated with signal %i - %s",
                        pid, this->cmd_term_sig.value(), strsignal(this->cmd_term_sig.value())
                    );
                    */
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
        }

        if (not cmd_succeeded())
        {
            if (cmd_exit_code.has_value() and cmd_exit_code.value() != 0)
                logger->log(LOG_ERROR, "Command %s exited with non-zero exit_code: %i", cmd_id.c_str(), cmd_exit_code.value());
            if (cmd_term_sig.has_value())
                logger->log(LOG_ERROR, "Command %s terminated with signal: %i / %s", cmd_id.c_str(), cmd_term_sig.value(), strsignal(cmd_term_sig.value()));

            logger->log(LOG_DEBUG5, "Command %s STDOUT: %s", cmd_id.c_str(), cmd_stdout.c_str());
            logger->log(LOG_DEBUG5, "Command %s STDERR: %s", cmd_id.c_str(), cmd_stderr.c_str());
        }
    }

    this->cmd_runtime_end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / 1000;
}
