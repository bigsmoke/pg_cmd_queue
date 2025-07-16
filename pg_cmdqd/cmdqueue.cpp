#include "cmdqueue.h"

#include <iostream>
#include <memory>

#include "pq-raii/libpq-raii.hpp"
#include "utils.h"

const std::string CmdQueue::CMD_QUEUE_RELNAME = "cmd_queue";

const std::string CmdQueue::SELECT_STMT = R"SQL(
    SELECT
        cmd_class_identity
        ,cmd_class_relname
        ,cmd_signature_class_relname
        ,queue_runner_role::text
        ,queue_notify_channel
        ,queue_reselect_interval_msec
        ,queue_reselect_randomized_every_nth
        ,queue_cmd_timeout_sec
        ,ansi_fg
    FROM
        cmdqd.cmd_queue
    WHERE
        (NULLIF($1, '{}'::text[]) IS NULL OR cmd_class = ANY ($1::regclass[]))
)SQL";

/**
 * @brief CmdQueue::CmdQueue
 * @param result
 * @param row_number
 * @param field_numbers
 */
CmdQueue::CmdQueue(const PG::result &result,
                   int row_number,
                   const std::unordered_map<std::string, int> &field_numbers
                  ) noexcept
{
    try
    {
        cmd_class_identity = PQ::getvalue(
                result,
                row_number,
                field_numbers.at("cmd_class_identity"));
        cmd_class_relname = PQ::getvalue(
                result,
                row_number,
                field_numbers.at("cmd_class_relname"));
        cmd_signature_class_relname = PQ::getvalue(
                result,
                row_number,
                field_numbers.at("cmd_signature_class_relname"));
        queue_runner_role = PQ::getnullable(
                result,
                row_number,
                field_numbers.at("queue_runner_role"));
        queue_notify_channel = PQ::getnullable(
                result,
                row_number,
                field_numbers.at("queue_notify_channel"));

        std::string queue_reselect_interval_msec = PQ::getvalue(
                result,
                row_number,
                field_numbers.at("queue_reselect_interval_msec"));
        this->queue_reselect_interval_msec = std::stoi(queue_reselect_interval_msec);

        if (not PQ::getisnull(result, row_number, field_numbers.at("queue_reselect_randomized_every_nth")))
        {
            std::string queue_reselect_randomized_every_nth = PQ::getvalue(
                    result,
                    row_number,
                    field_numbers.at("queue_reselect_randomized_every_nth"));
            this->queue_reselect_randomized_every_nth = std::stoi(queue_reselect_randomized_every_nth);
        }

        if (not PQ::getisnull(result, row_number, field_numbers.at("queue_cmd_timeout_sec")))
        {
            std::string queue_cmd_timeout_sec = PQ::getvalue(
                    result,
                    row_number,
                    field_numbers.at("queue_cmd_timeout_sec"));
            this->queue_cmd_timeout_sec = std::stod(queue_cmd_timeout_sec);
        }
        else
            this->queue_cmd_timeout_sec = 0;

        ansi_fg = PQgetvalue(result.get(), row_number, field_numbers.at("ansi_fg"));

        _is_valid = true;
    }
    catch (std::exception &ex)
    {
        _validation_error_message = formatString(
                "Error parsing cmd_queue metadata for `%s` queue: %s",
                this->cmd_class_identity.c_str(),
                ex.what());
        _is_valid = false;
    }
}

bool CmdQueue::is_valid() const
{
    return this->_is_valid;
}

std::string CmdQueue::validation_error_message() const
{
    return this->_validation_error_message;
}
