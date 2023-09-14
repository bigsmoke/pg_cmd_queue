#include "sqlqueuecmd.h"

#include <functional>
#include <regex>

#include "libpq-fe.h"

#include "utils.h"

const std::string SqlQueueCmd::SELECT_STMT_WITHOUT_RELNAME = R"SQL(
    SELECT
        queue_cmd_class::text as queue_cmd_class
        ,(parse_ident(queue_cmd_class::text))[
            array_upper(parse_ident(queue_cmd_class::text), 1)
        ] as queue_cmd_relname
        ,cmd_id
        ,cmd_subid
        ,extract(epoch from cmd_queued_since) as cmd_queued_since
        ,cmd_sql
    FROM
        cmdq.%s
    ORDER BY
        cmd_queued_since
    LIMIT 1
    FOR UPDATE SKIP LOCKED
)SQL";

const std::string SqlQueueCmd::UPDATE_STMT_WITHOUT_RELNAME = R"SQL(
    UPDATE
        cmdq.%s
    SET
        cmd_runtime = tstzrange(to_timestamp($3), to_timestamp($4))
        ,cmd_sql_result_status = $5
        ,cmd_sql_result_rows = $6
        ,cmd_sql_fatal_error = $7
        ,cmd_sql_nonfatal_errors = $8
    WHERE
        cmd_id = $1
        AND cmd_subid IS NOT DISTINCT from $2
)SQL";

std::string SqlQueueCmd::select_stmt(const CmdQueue &cmd_queue)
{
    return formatString(SELECT_STMT_WITHOUT_RELNAME, cmd_queue.queue_cmd_relname.c_str());
}

std::string SqlQueueCmd::update_stmt(const CmdQueue &cmd_queue)
{
    return formatString(UPDATE_STMT_WITHOUT_RELNAME, cmd_queue.queue_cmd_relname.c_str());
}

std::vector<std::optional<std::string>> SqlQueueCmd::update_params()
{
    std::vector<std::optional<std::string>> params;
    params.reserve(8);

    params.push_back(meta.cmd_id);
    params.push_back(meta.cmd_subid);
    params.push_back(formatString("%f", meta.cmd_runtime_start));
    params.push_back(formatString("%f", meta.cmd_runtime_end));
    params.push_back(cmd_sql_result_status);
    params.push_back(std::string("null"));  // TODO
    params.push_back(lwpg::to_nullable_string(cmd_sql_fatal_error));
    params.push_back(lwpg::to_string(cmd_sql_nonfatal_errors));

    return params;
}

SqlQueueCmd::SqlQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept
    : meta(result, row, fieldMapping)
{
    static std::regex leading_and_trailing_whitespace("^[\n\t ]+|[\n\t ]+$");

    if (not meta.is_valid()) {
        _is_valid = false;
    }

    try
    {
        if (PQgetisnull(result->get(), row, fieldMapping.at("cmd_sql")))
            throw std::domain_error("`cmd_sql` should never be `NULL`.");
        this->cmd_sql = PQgetvalue(result->get(), row, fieldMapping.at("cmd_sql"));
        this->cmd_sql = std::regex_replace(this->cmd_sql, leading_and_trailing_whitespace, "");
    }
    catch (std::exception &ex)
    {
        logger->log(LOG_ERROR, "Error parsing `%s` queue command data: %s", meta.queue_cmd_class.c_str(), ex.what());
        _is_valid = false;
    }
}

void SqlQueueCmd::receive_notice_c_wrapper(void *arg, const PGresult *res)
{
    auto member_function = std::bind(&SqlQueueCmd::receive_notice, (SqlQueueCmd*)arg, std::placeholders::_1);
    member_function(res);
}

void SqlQueueCmd::receive_notice(const PGresult *res)
{
    cmd_sql_nonfatal_errors.emplace_back(res);
}

lwpg::Error SqlQueueCmd::handle_sql_fatality(std::shared_ptr<lwpg::Result> &result)
{
    logger->log(
        LOG_ERROR, "cmd_id = '%s'%s: %s",
        meta.cmd_id.c_str(),
        meta.cmd_subid ? std::string(" (cmd_subid = '" + meta.cmd_subid.value() + "')").c_str() : "",
        PQresultErrorMessage(result->get())
    );

    return lwpg::Error(result);
}

void SqlQueueCmd::run_cmd(std::shared_ptr<lwpg::Conn> &conn)
{
    meta.stamp_start_time();

    // TODO: Check connection viability

    PQnoticeReceiver old_receiver = PQsetNoticeReceiver(conn->get(), SqlQueueCmd::receive_notice_c_wrapper, nullptr);

    logger->log(
        LOG_DEBUG3, "cmd_id = '%s'%s: %s",
        meta.cmd_id.c_str(),
        meta.cmd_subid ? std::string(" (cmd_subid = '" + meta.cmd_subid.value() + "')").c_str() : "",
        cmd_sql.c_str()
    );

    bool _sql_cmd_itself_has_failed = false;
    bool _sql_bookkeeping_has_failed = false;

    {
        // Set a savepoint so that, when the command errors, we don't have to rollback the whole
        // transaction (and lose the lock on the selected row).
        std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(
                conn->get(), "SAVEPOINT pre_run_cmd"));
        if (result->getResultStatus() != PGRES_COMMAND_OK)
        {
            _sql_bookkeeping_has_failed = true;
            cmd_sql_result_status = PQresStatus(result->getResultStatus());
            cmd_sql_fatal_error = handle_sql_fatality(result);
        }
    }

    if (not _sql_bookkeeping_has_failed)
    {
        std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(conn->get(), cmd_sql.c_str()));
        ExecStatusType exec_status = result->getResultStatus();
        cmd_sql_result_status = PQresStatus(exec_status);
        if (exec_status == PGRES_TUPLES_OK)
        {
            // TODO: retrieve rows
        }
        else if (exec_status == PGRES_COMMAND_OK) {}
        else
        {
            _sql_cmd_itself_has_failed = true;
            cmd_sql_fatal_error = handle_sql_fatality(result);
        }
    }

    // If no error occured yet, let's see what happens when we fire off all the constraints.
    if (not cmd_sql_fatal_error)
    {
        std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(
            conn->get(), "SET CONSTRAINTS ALL IMMEDIATE"));
        if (result->getResultStatus() != PGRES_COMMAND_OK)
        {
            _sql_cmd_itself_has_failed = true;
            cmd_sql_result_status = PQresStatus(result->getResultStatus());
            cmd_sql_fatal_error = handle_sql_fatality(result);
        }
    }

    if (not _sql_bookkeeping_has_failed)
    {
        if (_sql_cmd_itself_has_failed)
        {
            std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(
                conn->get(), "ROLLBACK TO SAVEPOINT pre_run_cmd"));
            if (result->getResultStatus() != PGRES_COMMAND_OK)
            {
                _sql_bookkeeping_has_failed = true;
                handle_sql_fatality(result);
            }
        }
        else
        {
            std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(
                conn->get(), "RELEASE SAVEPOINT pre_run_cmd"));
            if (result->getResultStatus() != PGRES_COMMAND_OK)
            {
                _sql_bookkeeping_has_failed = true;
                handle_sql_fatality(result);
            }
        }
    }

    PQsetNoticeReceiver(conn->get(), old_receiver, nullptr);

    meta.stamp_end_time();
}
