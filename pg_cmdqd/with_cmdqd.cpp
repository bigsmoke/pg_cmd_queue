#include <csignal>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fdguard.h"

const int CMDQD_SIGUSR1_TIMEOUT_S = 2;

// `psql` will only return exit codes between 0 and 3; therefore any exit code > 3 will be distinguishable.
const int EXIT_CMDQD_BIN_UNSPECIFIED = 4;
const int EXIT_CMDQD_FORK_ERROR = 5;
const int EXIT_CMDQD_SIGUSR1_TIMEOUT = 6;
const int EXIT_CMDQD_FAILURE = 7;
const int EXIT_PSQL_FORK_ERROR = 15;
const int EXIT_PSQL_WEIRD_WSTATUS = 18;
const int EXIT_PSQL_D_OPT_ARG_MISSING = 19;

const char ANSI_RESET[] = "\x1b[0m";
const char ANSI_RED[] = "\x1b[31m";
const char ANSI_BOLD[] = "\x1b[1m";
const char ANSI_NORMAL_INTENSITY[] = "\x1b[22m";

pid_t cmdqd_pid = -1;
pid_t psql_pid = -1;
bool cmdqd_is_ready = false;

void clean_exit(const int exit_code = 0)
{
    if (cmdqd_pid > 0)
    {
        kill(cmdqd_pid, SIGTERM);
        int wstatus;
        while (waitpid(cmdqd_pid, &wstatus, 0) < 0 and errno == EINTR);
        cmdqd_pid = -1;

        if (WIFSIGNALED(wstatus))
        {
            std::cerr << ANSI_RED << "with_cmdqd: "
                      << ANSI_BOLD << "pg_cmdqd" << ANSI_NORMAL_INTENSITY
                      << " aborted with termination signal "
                      << ANSI_BOLD << WTERMSIG(wstatus) << " / " << strsignal(WTERMSIG(wstatus))
                      << ANSI_RESET << std::endl;
            exit(EXIT_CMDQD_FAILURE);
        }
        assert(WIFEXITED(wstatus));
        int cmdqd_exit_code;
        if ((cmdqd_exit_code = WEXITSTATUS(wstatus)) != 0)
        {
            std::cerr << ANSI_RED << "with_cmdqd: "
                      << ANSI_BOLD << "pg_cmdqd" << ANSI_NORMAL_INTENSITY
                      << " failed with exit code "
                      << ANSI_BOLD << cmdqd_exit_code << ANSI_NORMAL_INTENSITY
                      << ANSI_RESET << std::endl;
            exit(EXIT_CMDQD_FAILURE);
        }
    }

    exit(exit_code);
}

void handle_signal(const int sig_num)
{
    bool cmdqd_is_running = cmdqd_pid > 0;

    // Did `pg_cmdqd` send us `SIGUSR1` to signal its readiness?
    if (sig_num == SIGUSR1 and cmdqd_is_running)
    {
        cmdqd_is_ready = true;
        return;
    }

    if (sig_num == SIGCHLD)
    {
        // Nothing to do; let's bounce back to `waitpid()` and let that reap the dead child.
        return;
    }

    // Any other signal, at any other time, we will take as a hint to kill ourselves.
    clean_exit(128 + sig_num);
}

int main(const int argc, const char *argv[])
{
    if (getenv("PG_CMDQD_BIN") == nullptr)
    {
        std::cerr << ANSI_RED << "with_cmdqd: " << ANSI_BOLD << "$PG_CMDQD_BIN" << ANSI_NORMAL_INTENSITY
                  << " environment variable not set." << ANSI_RESET << std::endl;
        exit(EXIT_CMDQD_BIN_UNSPECIFIED);
    }
    std::string cmdqd_path(getenv("PG_CMDQD_BIN"));

    std::optional<std::string> log_file;
    if (getenv("PG_CMDQD_LOG_FILE") != nullptr)
        log_file = std::string(getenv("PG_CMDQD_LOG_FILE"));

    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == "-d")
        {
            if (++i == argc)
            {
                std::cerr << ANSI_RED << "with_cmdqd: " << "missing argument to "
                          << ANSI_BOLD << "psql" << ANSI_NORMAL_INTENSITY << ANSI_NORMAL_INTENSITY
                          << "'s " << ANSI_BOLD << "-d" << ANSI_NORMAL_INTENSITY
                          << " option." << ANSI_RESET << std::endl;
                exit(EXIT_PSQL_D_OPT_ARG_MISSING);
            }
            setenv("PGDATABASE", argv[i], true);
        }
    }

    struct sigaction sig_handler;
    sig_handler.sa_handler = handle_signal;
    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_flags = 0;
    sigaction(SIGCHLD, &sig_handler, NULL);
    sigaction(SIGUSR1, &sig_handler, NULL);
    sigaction(SIGTERM, &sig_handler, NULL);
    sigaction(SIGINT, &sig_handler, NULL);
    sigaction(SIGQUIT, &sig_handler, NULL);

    cmdqd_pid = fork();
    if (cmdqd_pid < 0)
    {
        std::cerr << ANSI_RED << "with_cmdqd: " << "fork() failure: " << strerror(errno)
                  << ANSI_RESET << std::endl;
        exit(EXIT_CMDQD_FORK_ERROR);
    }
    if (cmdqd_pid == 0)
    {
        if (log_file)
        {
            FdGuard log_file_fd(open(log_file.value().c_str(), O_WRONLY|O_TRUNC|O_CREAT,0644));
            dup2(log_file_fd.fd(), STDOUT_FILENO);
        }
        char const *cmdqd_argv[] = {
            cmdqd_path.c_str(),
            "--emit-sigusr1-when-ready",
            "--log-level",
            "LOG_DEBUG5",
            "--no-log-times",
            nullptr,
        };
        execvp(cmdqd_path.c_str(), (char * const *)cmdqd_argv);

        std::cerr << ANSI_RED << "with_cmdqd: " << "execvp() failure: " << strerror(errno)
                  << ANSI_RESET << std::endl;
        exit(127);
    }
    assert(cmdqd_pid > 0);

    sleep(CMDQD_SIGUSR1_TIMEOUT_S);

    if (not cmdqd_is_ready)
    {
        std::string cmdqd_basename = cmdqd_path.substr(cmdqd_path.find_last_of("/") + 1);
        std::cerr << ANSI_RED << "with_cmdqd: " << "Timeout expired while waiting to receive the "
                  << ANSI_BOLD << "SIGUSR1" << ANSI_NORMAL_INTENSITY
                  << " signal from "
                  << ANSI_BOLD << cmdqd_basename << ANSI_NORMAL_INTENSITY
                  << ANSI_RESET << std::endl;
        clean_exit(EXIT_CMDQD_SIGUSR1_TIMEOUT);
    }

    psql_pid = fork();
    if (psql_pid < 0)
    {
        std::cerr << ANSI_RED << "with_cmdqd: " << "fork() failure: " << strerror(errno) << ANSI_RESET << std::endl;
        exit(EXIT_PSQL_FORK_ERROR);
    }
    if (psql_pid == 0)
    {
        //std::vector<std::string> psql_argv(&argv[1], argv + argc - 1);
        const char *psql_argv[argc];
        for (int i = 1; i < argc; i++) psql_argv[i-1] = argv[i];
        psql_argv[argc - 1] = nullptr;

        execvp(psql_argv[0], (char * const *)psql_argv);

        std::cerr << ANSI_RED << "with_cmdqd: " << "execvp() failure: " << strerror(errno)
                  << ANSI_RESET << std::endl;
        exit(127);
    }
    assert (psql_pid > 0);
    int wstatus;
    while (waitpid(psql_pid, &wstatus, 0) < 0 and errno == EINTR);
    psql_pid = -1;

    if (WIFEXITED(wstatus))
        clean_exit(WEXITSTATUS(wstatus));
    if (WIFSIGNALED(wstatus))
        clean_exit(128 + WTERMSIG(wstatus));

    clean_exit(EXIT_PSQL_WEIRD_WSTATUS);
}
