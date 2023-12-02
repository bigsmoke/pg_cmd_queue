#include "sqlqueuecmd.h"

#include <functional>
#include <regex>

#include "pq-raii/libpq-raii.hpp"
#include "utils.h"

/*
std::string SqlQueueCmd::select::notify(const CmdQueue &cmd_queue)
{
    return formatString(SELECT_TEMPLATE, cmd_queue.queue_cmd_relname.c_str(), "", "queued_since");
}
*/

std::vector<std::optional<std::string>> SqlQueueCmd::update_params()
{
    std::vector<std::optional<std::string>> params;
    params.reserve(8);

    params.push_back(meta.cmd_id);
    params.push_back(meta.cmd_subid);
    params.push_back(formatString("%f", meta.cmd_runtime_start));
    params.push_back(formatString("%f", meta.cmd_runtime_end));
    params.push_back(cmd_sql_result_status);
    params.push_back(std::string("null"));

    if (cmd_sql_fatal_error)
        params.push_back(
            PQ::as_text_composite_value(
                PQ::values_to_vector<char, std::optional<std::string>>(cmd_sql_fatal_error.value())));
    else
        params.push_back(std::nullopt);

    {
        std::vector<std::string> composite_values;
        composite_values.reserve(cmd_sql_nonfatal_errors.size());
        for (const std::map<char, std::optional<std::string>> &err : cmd_sql_nonfatal_errors)
        {
            composite_values.push_back(PQ::as_text_composite_value(
                        PQ::values_to_vector<char, std::optional<std::string>>(err)));
        }
        params.push_back(PQ::as_text_array(composite_values));
    }

    return params;
}

SqlQueueCmd::SqlQueueCmd(
        std::shared_ptr<PG::result> &result,
        int row,
        const std::unordered_map<std::string, int> &fieldMapping) noexcept
    : meta(result, row, fieldMapping)
{
    static std::regex leading_and_trailing_whitespace("^[\n\t ]+|[\n\t ]+$");

    if (not meta.is_valid()) {
        _is_valid = false;
    }

    try
    {
        if (PQ::getisnull(result, row, fieldMapping.at("cmd_sql")))
            throw std::domain_error("`cmd_sql` should never be `NULL`.");
        this->cmd_sql = PQ::getvalue(result, row, fieldMapping.at("cmd_sql"));
        this->cmd_sql = std::regex_replace(this->cmd_sql, leading_and_trailing_whitespace, "");
    }
    catch (std::exception &ex)
    {
        logger->log(LOG_ERROR, "Error parsing `sql_queue_cmd` data: %s", ex.what());
        _is_valid = false;
    }
}

void SqlQueueCmd::receive_notice_c_wrapper(void *arg, const PGresult *res)
{
    auto member_function = std::bind(&SqlQueueCmd::receive_notice,
                                     (SqlQueueCmd*)arg,
                                     std::placeholders::_1);
    member_function(res);
}

void SqlQueueCmd::receive_notice(const PGresult *res)
{
    std::shared_ptr<PG::result> res_raii = std::make_shared<PG::result>((PGresult *)res);
    std::map<char, std::optional<std::string>> nonfatal_error = PQ::resultErrorFields(res_raii);
    cmd_sql_nonfatal_errors.emplace_back(nonfatal_error);
}

std::optional<std::map<char, std::optional<std::string>>>
SqlQueueCmd::handle_sql_fatality(std::shared_ptr<PG::result> &result)
{
    logger->log(
        LOG_ERROR, "cmd_id = '%s'%s: %s",
        meta.cmd_id.c_str(),
        meta.cmd_subid ? std::string(" (cmd_subid = '" + meta.cmd_subid.value() + "')").c_str() : "",
        PQresultErrorMessage(result->get())
    );

    return PQ::resultErrorFields(result);
}

void SqlQueueCmd::run_cmd(std::shared_ptr<PG::conn> &conn, const double queue_cmd_timeout_sec)
{
    // TODO: Check connection viability

    PQnoticeReceiver old_receiver = PQsetNoticeReceiver(conn->get(),
                                                        SqlQueueCmd::receive_notice_c_wrapper,
                                                        nullptr);

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
        std::shared_ptr<PG::result> result = PQ::exec(conn, "SAVEPOINT pre_run_cmd");
        if (PQ::resultStatus(result) != PGRES_COMMAND_OK)
        {
            _sql_bookkeeping_has_failed = true;
            cmd_sql_result_status = PQresStatus(PQ::resultStatus(result));
            cmd_sql_fatal_error = handle_sql_fatality(result);
        }
    }

    if (not _sql_bookkeeping_has_failed)
    {
        std::shared_ptr<PG::result> result = PQ::exec(conn, cmd_sql.c_str());
        ExecStatusType exec_status = PQ::resultStatus(result);
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
        std::shared_ptr<PG::result> result = PQ::exec(conn, "SET CONSTRAINTS ALL IMMEDIATE");
        if (PQ::resultStatus(result) != PGRES_COMMAND_OK)
        {
            _sql_cmd_itself_has_failed = true;
            cmd_sql_result_status = PQresStatus(PQ::resultStatus(result));
            cmd_sql_fatal_error = handle_sql_fatality(result);
        }
    }

    if (not _sql_bookkeeping_has_failed)
    {
        if (_sql_cmd_itself_has_failed)
        {
            std::shared_ptr<PG::result> result = PQ::exec(conn, "ROLLBACK TO SAVEPOINT pre_run_cmd");
            if (PQ::resultStatus(result) != PGRES_COMMAND_OK)
            {
                _sql_bookkeeping_has_failed = true;
                handle_sql_fatality(result);
            }
        }
        else
        {
            std::shared_ptr<PG::result> result = PQ::exec(conn, "RELEASE SAVEPOINT pre_run_cmd");
            if (PQ::resultStatus(result) != PGRES_COMMAND_OK)
            {
                _sql_bookkeeping_has_failed = true;
                handle_sql_fatality(result);
            }
        }
    }

    PQsetNoticeReceiver(conn->get(), old_receiver, nullptr);
}
