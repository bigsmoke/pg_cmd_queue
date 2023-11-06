#include "nixqueuecmd.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <numeric>
#include <iostream>
#include <memory>
#include <string>

#include "base64.hpp"
#include "pq-raii/libpq-raii.hpp"
#include "cmdqueue.h"
#include "fdguard.h"
#include "pipefds.h"
#include "utils.h"

#define CMDQD_PIPE_BUFFER_SIZE 512

const int GRACE_SECONDS_BETWEEN_SIGTERM_AND_SIGKILL = 1;

std::string NixQueueCmd::select_stmt(
        const CmdQueue &cmd_queue,
        const std::optional<std::string> &where,
        const std::optional<std::string> &order_by)
{
    return std::string(R"SQL(
SELECT
    queue_cmd_class::text AS queue_cmd_class
    ,(parse_ident(queue_cmd_class::text))[
        array_upper(parse_ident(queue_cmd_class::text), 1)
    ] AS queue_cmd_relname
    ,cmd_id
    ,cmd_subid
    ,extract(epoch from cmd_queued_since) AS cmd_queued_since
    ,cmd_argv
    ,cmd_env
    ,convert_from(cmd_stdin, 'UTF8') AS cmd_stdin
FROM
    cmdq.)SQL" + cmd_queue.queue_cmd_relname /* TODO: escape relname */ + R"SQL(
WHERE
    cmd_runtime IS NULL)SQL" + ( where ? R"SQL(
    AND )SQL" + where.value() : "") + R"SQL(
)SQL" + (order_by ? R"SQL(
ORDER BY
    )SQL" + order_by.value() : "") + R"SQL(
LIMIT 1
FOR UPDATE SKIP LOCKED
)SQL");
}

const std::string NixQueueCmd::UPDATE_STMT_WITHOUT_RELNAME = R"SQL(
    UPDATE
        cmdq.%s
    SET
        cmd_runtime = tstzrange(to_timestamp($3), to_timestamp($4))
        ,cmd_exit_code = $5
        ,cmd_term_sig = $6
        ,cmd_stdout = convert_to($7, 'UTF8')
        ,cmd_stderr = convert_to($8, 'UTF8')
    WHERE
        cmd_id = $1
        AND cmd_subid IS NOT DISTINCT from $2
)SQL";

/*
std::string NixQueueCmd::select::notify(const CmdQueue &cmd_queue)
{
    return formatString(SELECT_TEMPLATE, cmd_queue.queue_cmd_relname.c_str(), "", "queued_since");
}
*/

std::string NixQueueCmd::update_stmt(const std::shared_ptr<PG::conn> &conn)
{
    return formatString(R"SQL(
    UPDATE
        %s
    SET
        cmd_runtime = tstzrange(to_timestamp(%f), to_timestamp(%f))
        ,cmd_exit_code = %s
        ,cmd_term_sig = %s
        ,cmd_stdout = decode('%s', 'base64')
        ,cmd_stderr = decode('%s', 'base64')
    WHERE
        cmd_id = '%s'
        AND cmd_subid IS NOT DISTINCT from %s
)SQL",
            meta.queue_cmd_class.c_str(),
            meta.cmd_runtime_start,
            meta.cmd_runtime_end,
            cmd_exit_code ? std::to_string(cmd_exit_code.value()).c_str() : "NULL",
            cmd_term_sig ? std::to_string(cmd_term_sig.value()).c_str() : "NULL",
            base64::to_base64(cmd_stdout).c_str(),
            base64::to_base64(cmd_stderr).c_str(),
            meta.cmd_id.c_str(),
            meta.cmd_subid ? PQ::escapeLiteral(conn, meta.cmd_subid.value()).c_str() : "NULL"
        );
}

std::string NixQueueCmd::update_stmt(const CmdQueue &cmd_queue)
{
    return formatString(UPDATE_STMT_WITHOUT_RELNAME, cmd_queue.queue_cmd_relname.c_str());
}

std::vector<std::optional<std::string>> NixQueueCmd::update_params()
{
    std::vector<std::optional<std::string>> params;
    params.reserve(8);

    params.push_back(meta.cmd_id);
    params.push_back(meta.cmd_subid);
    params.push_back(formatString("%f", meta.cmd_runtime_start));
    params.push_back(formatString("%f", meta.cmd_runtime_end));
    params.push_back(PQ::as_text(cmd_exit_code));
    params.push_back(PQ::as_text(cmd_term_sig));
    params.push_back(cmd_stdout);
    params.push_back(cmd_stderr);

    return params;
}

NixQueueCmd::NixQueueCmd(std::shared_ptr<PG::result> &result, int row_number, const std::unordered_map<std::string, int> &field_numbers) noexcept
    : meta(result, row_number, field_numbers)
{
    try
    {
        if (PQgetisnull(result->get(), row_number, field_numbers.at("cmd_argv")))
        {
            throw std::domain_error("`cmd_argv` should never be `NULL`.");
        }
        std::string raw_cmd_argv = PQgetvalue(result->get(), row_number, field_numbers.at("cmd_argv"));
        cmd_argv = PQ::from_text_array(raw_cmd_argv);

        if (PQgetisnull(result->get(), row_number, field_numbers.at("cmd_env")))
        {
            throw std::domain_error("`cmd_env` should never be `NULL`.");
        }
        std::string raw_cmd_env = PQgetvalue(result->get(), row_number, field_numbers.at("cmd_env"));
        cmd_env = PQ::from_text_hstore(raw_cmd_env);

        // For `cmd_stdin`, we can ignore NULLness, because `PGgetvalue()` returns an empty string when the
        // field is `NULL`, which is what we'd want anyway.
        cmd_stdin = PQgetvalue(result->get(), row_number, field_numbers.at("cmd_stdin"));

        _is_valid = true;
    }
    catch (std::exception &ex)
    {
        logger->log(LOG_ERROR, "Error parsing `nix_queue_cmd` data: %s", ex.what());
        _is_valid = false;
    }
}

NixQueueCmd::~NixQueueCmd()
{
}

std::string NixQueueCmd::cmd_line() const
{
    static std::regex re("^.*[ \"$?!&%#,;].*$");
    std::string line;

    int i = 0;
    for (const std::string &arg : this->cmd_argv)
    {
        if (i++ > 0)
            line += " ";
        line += std::regex_replace(arg, re, "\"$&\"");
    }

    return line;
}

bool NixQueueCmd::cmd_succeeded() const
{
    return cmd_exit_code.has_value() and cmd_exit_code.value() == 0;
}

/*
void NixQueueCmd::_run_fork_parent_logic()
{
}

void NixQueueCmd::_run_forked_child_logic()
{
}
*/

void sigchld_handler(int _)
{
}

void NixQueueCmd::run_cmd(std::shared_ptr<PG::conn> &conn, const double queue_cmd_timeout_sec)
{
    static bool sigchld_handler_is_set = false;
    if (not sigchld_handler_is_set)
    {
        // We will set up a dummy signal handler function for `SIGCHLD`, because all we really want is for
        // `poll()` to return when the child process exits.
        struct sigaction sigchld_action;
        sigemptyset(&sigchld_action.sa_mask);
        sigchld_action.sa_handler = sigchld_handler;
        sigchld_action.sa_flags = 0;
        sigaction(SIGCHLD, &sigchld_action, nullptr);
        sigchld_handler_is_set = true;
    }

    logger->log(
        LOG_DEBUG3, "cmd_id = '%s'%s: \x1b[1m%s\x1b[22m",
        meta.cmd_id.c_str(),
        meta.cmd_subid ? std::string(" (cmd_subid = '" + meta.cmd_subid.value() + "')").c_str() : "",
        cmd_line().c_str()
    );

    PipeFds stdin_fds, stdout_fds, stderr_fds;

    // We temporarily mask signals that are normally sent to the whole process _group_, until we've done
    // a successful fork and detached the child process from our process group.  This way, we can keep
    // these signals from interrupting a running `nix_queue_cmd` process.
    sigset_t sig_mask, old_sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGINT);
    sigaddset(&sig_mask, SIGQUIT);
    sigprocmask(SIG_SETMASK, &sig_mask, &old_sig_mask);

    pid_t pid = fork();
    if (pid == -1)
    {
        logger->log(LOG_ERROR, "fork() failed: %s", strerror(errno));
        cmd_stdout = "";
        cmd_stderr = formatString("fork() failed: %s", strerror(errno));
        cmd_term_sig = SIGABRT;
        return;
    }

    if (pid == 0)  // pid == 0 ⇒ We're in a forked child process
    {
        // Close the end of each pipe that we won't need in the child process
        stdin_fds.close_write_fd();
        stdout_fds.close_read_fd();
        stderr_fds.close_read_fd();

        while ((dup2(stdin_fds.read_fd(), STDIN_FILENO) == -1) && (errno == EINTR)) {}
        while ((dup2(stdout_fds.write_fd(), STDOUT_FILENO) == -1) && (errno == EINTR)) {}
        while ((dup2(stderr_fds.write_fd(), STDERR_FILENO) == -1) && (errno == EINTR)) {}

        // Brute force close any open file/socket, because we don't want the child to see them.
        struct rlimit rlim;
        memset(&rlim, 0, sizeof (struct rlimit));
        getrlimit(RLIMIT_NOFILE, &rlim);
        for (rlim_t i = 3; i < rlim.rlim_cur; ++i) close (i);

        if (setpgid(0, 0) < 0)
        {
            std::cerr << strerror(errno) << std::endl;
            exit(128);  // Arbitrarily chosen exit code.
        }

        // Now that we've detached ourselves from our parent process group, we can safely restore the default
        // signal mask—the `exec*()` functions will already restore the default signal _handlers_—without us
        // risking to receive signals intended for our parent process (`pg_cmdqd`).
        sigset_t empty_sigset;
        sigemptyset(&empty_sigset);
        sigprocmask(SIG_SETMASK, &empty_sigset, nullptr);

        std::vector<char *> argv_heads;
        argv_heads.reserve(this->cmd_argv.size() + 1);
        for (const std::string &s : cmd_argv)
            argv_heads.push_back(const_cast<char*>(s.c_str()));
        argv_heads.push_back(nullptr);
        char **c_argv = argv_heads.data();
        // We don't have to worry about cleaning up c_argv, because execvp() will clear up all that.

        for (const std::pair<const std::string, std::string> &var : this->cmd_env)
        {
            setenv(var.first.c_str(), var.second.c_str(), 1);
        }

        execvp(this->cmd_argv[0].c_str(), c_argv);

        // We only get here if the call to `execvp()` fails.
        std::cerr << strerror(errno) << std::endl;
        exit(127);  // Same as when bash can't find a command.
    }

    assert(pid > 0);  // We're in the parent process.

    if (setpgid(pid, pid) < 0 and errno != EACCES)
    {
        // We have a `setpgid()` error _other_ than that the child process already did a successful
        // `setpgid()` on itself.
        this->cmd_stderr = std::string("setpgid() error: ") + strerror(errno) + "\n";
        this->cmd_term_sig = SIGABRT;
        return;
    }

    // Now that we've detached the child process from our own process group, we can safely restore our
    // previous signal mask, without the risk of signals that are intended for us propagating to a running
    // `nix_queue_cmd` process.
    if (sigprocmask(SIG_SETMASK, &old_sig_mask, nullptr) < 0)
    {
        this->cmd_stderr = std::string("sigprocmask() error: ") + strerror(errno) + "\n";
        this->cmd_term_sig = SIGABRT;
        return;
    }

    logger->log(
        LOG_DEBUG4, "cmd_id = '%s'%s: fork() child PID = \x1b[1m%jd\x1b[22m",
        meta.cmd_id.c_str(),
        meta.cmd_subid ? std::string(" (cmd_subid = '" + meta.cmd_subid.value() + "')").c_str() : "",
        (intmax_t) pid
    );

    // Close the end of each pipe that we won't need in the parent process
    stdin_fds.close_read_fd();
    stdout_fds.close_write_fd();
    stderr_fds.close_write_fd();

    // Make the parent ends of the pipes non-blocking
    fcntl(stdin_fds.write_fd(), F_SETFL, fcntl(stdin_fds.write_fd(), F_GETFL) | O_NONBLOCK);
    fcntl(stdout_fds.read_fd(), F_SETFL, fcntl(stdout_fds.read_fd(), F_GETFL) | O_NONBLOCK);
    fcntl(stderr_fds.read_fd(), F_SETFL, fcntl(stderr_fds.read_fd(), F_GETFL) | O_NONBLOCK);

    struct pollfd fds[] = {
        { stdin_fds.write_fd(), static_cast<short>((cmd_stdin.empty() ? 0 : POLLOUT) | POLLHUP | POLLERR), 0 },
        { stdout_fds.read_fd(), POLLIN | POLLHUP | POLLERR, 0 },
        { stderr_fds.read_fd(), POLLIN | POLLHUP | POLLERR, 0 },
    };

    char stdout_buf[CMDQD_PIPE_BUFFER_SIZE];
    char stderr_buf[CMDQD_PIPE_BUFFER_SIZE];
    ssize_t cum_stdin_bytes_written = 0;
    bool tried_sigterm = false;
    double sigterm_time = 0;

    int wstatus;
    int res_pid = 0;

    while (true)
    {
        // Let's check if maybe we were just interrupted by a SIGCHLD signal that we're interested in.
        // Yes, I (Rowan) am missing `pidfd_open()` on MacOS :'-(
        if ((res_pid = waitpid(pid, &wstatus, WNOHANG)) != 0) break;

        double now = QueueCmdMetadata::unix_timestamp();
        int poll_timeout;
        if (queue_cmd_timeout_sec == 0)
        {
            poll_timeout = -1;
        }
        else
        {
            poll_timeout = (queue_cmd_timeout_sec
                            - (now - meta.cmd_runtime_start)
                            + (tried_sigterm ? GRACE_SECONDS_BETWEEN_SIGTERM_AND_SIGKILL : 0)) * 1000;
            if (poll_timeout < 0)
                poll_timeout = 0;
        }
        int fd_count = poll(fds, 3, poll_timeout);
        if (fd_count < 0)
        {
            if (errno == EINTR) continue;

            this->cmd_stderr = formatString("poll() error: %s", strerror(errno));
            this->cmd_term_sig = SIGABRT;
            break;
        }
        if (fd_count == 0)
        {
            now = QueueCmdMetadata::unix_timestamp();
            if (now - meta.cmd_runtime_start >= queue_cmd_timeout_sec)
            {
                // Have we tried it friendly already?
                if (tried_sigterm)
                {
                    // And given the cmd a second to shutdown gracefully?
                    if ((now - sigterm_time) > GRACE_SECONDS_BETWEEN_SIGTERM_AND_SIGKILL)
                    {
                        logger->log(
                                LOG_ERROR,
                                "We tried to kill PID %i gently, %f seconds ago; now we will SIGKILL it.",
                                pid,
                                now - sigterm_time);
                        kill(pid, SIGKILL);  // `kill -9`
                        res_pid = waitpid(pid, &wstatus, 0);
                        break;
                    }
                }
                else
                {
                    logger->log(
                            LOG_ERROR,
                            "queue_cmd_timeout of %fsec exceeded; sending SIGTERM signal to PID %i",
                            queue_cmd_timeout_sec,
                            pid);
                    kill(pid, SIGTERM);
                    tried_sigterm = true;
                    sigterm_time = now;
                }
            }
        } // if (fd_count == 0)
        if (fd_count > 0)
        {
            logger->log(LOG_DEBUG5, "fds[].revents: %i, %i, %i", fds[0].revents, fds[1].revents, fds[2].revents);

            if (fds[0].revents & POLLOUT)
            {
                logger->log(LOG_DEBUG5, "cmd STDIN ready for write()");
                bool write_to_stdin_erred = false;
                while (cum_stdin_bytes_written < (ssize_t)cmd_stdin.length())
                {
                    //logger->log(LOG_DEBUG5, "Let's write() %i bytes TO STDIN", this->cmd_stdin.length()-cum_stdin_bytes_written);
                    ssize_t stdin_bytes_written = write(
                        stdin_fds.write_fd(),
                        this->cmd_stdin.c_str()+cum_stdin_bytes_written,
                        this->cmd_stdin.length()-cum_stdin_bytes_written
                    );
                    if (stdin_bytes_written < 0)
                    {
                        if (errno == EINTR) continue;
                        this->cmd_stderr = formatString("Error during write() to cmd STDIN: %s", strerror(errno));
                        this->cmd_term_sig = SIGABRT;
                        write_to_stdin_erred = true;
                        break;
                    }
                    cum_stdin_bytes_written += stdin_bytes_written;
                }
                if (write_to_stdin_erred) break;
                if (cum_stdin_bytes_written == (ssize_t)cmd_stdin.length())
                {
                    fds[0].events = 0;
                }
            }
            if (fds[1].revents & POLLIN)
            {
                logger->log(LOG_DEBUG5, "cmd STDOUT ready for read()");
                ssize_t stdout_bytes_read = 0;
                while ((stdout_bytes_read = read(stdout_fds.read_fd(), stdout_buf, CMDQD_PIPE_BUFFER_SIZE)) > 0)
                {
                    this->cmd_stdout += std::string(stdout_buf, stdout_bytes_read);
                }
                if (stdout_bytes_read < 0 and errno != EAGAIN)
                {
                    this->cmd_stderr = formatString("Error during read() from cmd STDOUT: %s", strerror(errno));
                    this->cmd_term_sig = SIGABRT;
                    break;
                }
            }
            if (fds[2].revents & POLLIN)
            {
                logger->log(LOG_DEBUG5, "cmd STDERR ready for read()");
                ssize_t stderr_bytes_read = 0;
                while ((stderr_bytes_read = read(stderr_fds.read_fd(), stderr_buf, CMDQD_PIPE_BUFFER_SIZE)) > 0)
                {
                    this->cmd_stderr += std::string(stderr_buf, stderr_bytes_read);
                }
                if (stderr_bytes_read < 0 and errno != EAGAIN)
                {
                    this->cmd_stderr = formatString("Error during read() from cmd STDERR: %s", strerror(errno));
                    this->cmd_term_sig = SIGABRT;
                    break;
                }
            }

            if (fds[0].revents & (POLLERR | POLLHUP))
            {
                // The `poll()` man page says that, for STDIN, setting `fd = -1` won't make it be ignored.
                // Luckily, we're in the parent process, and the STDIN of our child is not _our_ STDIN /
                // FD 0.
                fds[0].fd = -1;
            }
            if (fds[1].revents & (POLLERR | POLLHUP))
            {
                fds[1].fd = -1;
            }
            if (fds[2].revents & (POLLERR | POLLHUP))
            {
                fds[2].fd = -1;
            }
        } // if (fd_count > 0)
    } // while (true)

    if (res_pid == 0) res_pid = waitpid(pid, &wstatus, WNOHANG);

    if (res_pid < 0)
    {
        this->cmd_term_sig = -1;
        this->cmd_stderr += formatString(
            "Unexpected error while calling `waitpid(%i, &wstatus, WNOHANG)`: '%s'",
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
            logger->log(
                LOG_ERROR, "cmd_id = '%s'%s: exited with non-zero exit_code: %i",
                meta.cmd_id.c_str(),
                meta.cmd_subid ? std::string(" (cmd_subid = '" + meta.cmd_subid.value() + "')").c_str() : "",
                cmd_exit_code.value()
            );
        if (cmd_term_sig.has_value())
            logger->log(LOG_ERROR, "Command %s terminated with signal: %i / %s", meta.cmd_id.c_str(), cmd_term_sig.value(), strsignal(cmd_term_sig.value()));

        logger->log(LOG_DEBUG5, "Command %s STDOUT: %s", meta.cmd_id.c_str(), cmd_stdout.c_str());
        logger->log(LOG_DEBUG5, "Command %s STDERR: %s", meta.cmd_id.c_str(), cmd_stderr.c_str());
    }
}
