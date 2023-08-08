#include "nixqueuecmd.h"

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <memory>

#include "lwpg_result.h"
#include "lwpg_array.h"
#include "lwpg_hstore.h"
#include "utils.h"

const std::string NixQueueCmd::SELECT_STMT_WITHOUT_RELNAME = R"SQL(
    SELECT
        queue_cmd_class::text as queue_cmd_class
        ,cmd_id
        ,cmd_subid
        ,extract(epoch from cmd_queued_since) as cmd_queued_since
        ,cmd_argv
        ,cmd_env
        ,cmd_stdin
    FROM
        cmdq.%s
)SQL";

NixQueueCmd::NixQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept
{
    try
    {
        queue_cmd_class = PQgetvalue(result->get(), row, fieldMapping.at("queue_cmd_class"));

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

std::string NixQueueCmd::select(std::string& local_relname)
{
    return formatString(SELECT_STMT_WITHOUT_RELNAME, local_relname.c_str());
}

bool NixQueueCmd::is_valid() const
{
    return _is_valid;
}

void NixQueueCmd::run_cmd()
{
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }
    pid_t pid = fork();
    switch (pid) {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        case 0:
            puts("Child exiting.");
            exit(EXIT_SUCCESS);
        default:
            printf("Child is PID %jd\n", (intmax_t) pid);
            puts("Parent exiting.");
            exit(EXIT_SUCCESS);
    }
}
