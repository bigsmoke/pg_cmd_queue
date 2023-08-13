#include "nixqueuecmdcontext.h"

#include <postgresql/libpq-fe.h>

#include "utils.h"

const std::string NixQueueCmdContext::SELECT_STMT_WITHOUT_RELNAME = R"SQL(
    SELECT
        queue_cmd_class::text as queue_cmd_class
        ,parse_ident(queue_cmd_class::text))[
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

const std::string NixQueueCmdContext::UPDATE_STMT_WITHOUT_RELNAME = R"SQL(
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

const std::unordered_map<std::string, int> NixQueueCmdContext::SELECT_FIELD_MAPPING = {
    {"queue_cmd_class", 0},
    {"queue_cmd_relname", 1},
    {"cmd_id", 2},
    {"cmd_subid", 3},
    {"cmd_queued_since", 4},
    {"cmd_argv", 5},
    {"cmd_env", 6},
    {"cmd_stdin", 7},
};

std::string NixQueueCmdContext::select_stmt()
{
    return formatString(SELECT_STMT_WITHOUT_RELNAME, _cmd_queue.queue_cmd_relname.c_str());
}

std::string NixQueueCmdContext::update_stmt()
{
    return formatString(UPDATE_STMT_WITHOUT_RELNAME, _cmd_queue.queue_cmd_relname.c_str());
}

NixQueueCmdContext::NixQueueCmdContext(std::shared_ptr<lwpg::Conn> conn, const CmdQueue &cmd_queue)
    : _conn(conn), _cmd_queue(cmd_queue)
{
}

NixQueueCmd NixQueueCmdContext::select_for_update()
{
    if (!_conn.get())
        throw std::runtime_error("No connection");

    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(
            _conn.get()->get(), select_stmt().c_str()));

    if (result->getResultStatus() != PGRES_TUPLES_OK)
    {
        std::string error(PQerrorMessage(_conn.get()->get()));
        throw std::runtime_error(error);
    }

    int row_count = PQntuples(result->get());
    if (row_count > 0)
    {
        throw std::runtime_error("Too many rows");
    }

    NixQueueCmd nix_queue_cmd(result, 0, SELECT_FIELD_MAPPING);
    return nix_queue_cmd;
}

void NixQueueCmdContext::update(const NixQueueCmd &nix_queue_cmd)
{
    if (!_conn.get())
        throw std::runtime_error("No connection");

    std::vector<lwpg::nullable_string> params(8);
    params.push_back(nix_queue_cmd.cmd_id);
    params.push_back(nix_queue_cmd.cmd_subid);
    params.push_back(formatString("%d", nix_queue_cmd.cmd_runtime_start));
    params.push_back(formatString("%d", nix_queue_cmd.cmd_runtime_end));
    params.push_back(lwpg::to_nullable_string(nix_queue_cmd.cmd_exit_code));
    params.push_back(lwpg::to_nullable_string(nix_queue_cmd.cmd_term_sig));
    params.push_back(nix_queue_cmd.cmd_stdout);
    params.push_back(nix_queue_cmd.cmd_stderr);
    const char* values[params.size()];
    int i = 0;
    for (const lwpg::nullable_string &param : params)
    {
        if (param.has_value())
            values[i++] = param.value().c_str();
        else
            values[i++] = nullptr;
    }

    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexecParams(
        _conn.get()->get(),
        update_stmt().c_str(),
        params.size(),
        nullptr,
        values,
        nullptr,
        nullptr,
        0
    ));

    if (result->getResultStatus() != PGRES_TUPLES_OK)
    {
        std::string error(PQerrorMessage(_conn.get()->get()));
        throw std::runtime_error(error);
    }
}
