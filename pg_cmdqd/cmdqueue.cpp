#include "cmdqueue.h"

#include <iostream>
#include <memory>

#include "pq-raii/libpq-raii.hpp"
#include "utils.h"

const std::string CmdQueue::CMD_QUEUE_RELNAME = "cmd_queue";

const std::string CmdQueue::SELECT_STMT = R"SQL(
    SELECT
        (parse_ident(queue_cmd_class::regclass::text))[
            array_upper(parse_ident(queue_cmd_class::regclass::text), 1)
        ] as queue_cmd_relname
        ,(parse_ident(queue_signature_class::regclass::text))[
            array_upper(parse_ident(queue_signature_class::regclass::text), 1)
        ] as queue_signature_class
        ,queue_runner_role::text
        ,queue_notify_channel
        ,extract('epoch' from queue_reselect_interval) * 10^3 as queue_reselect_interval_msec
        ,queue_reselect_randomized_every_nth
        ,color.ansi_fg
    FROM
        cmdq.cmd_queue
    CROSS JOIN LATERAL
        cmdq.queue_cmd_class_color(queue_cmd_class) as color
    WHERE
        NULLIF($1, '{}'::text[]) IS NULL OR queue_cmd_class = ANY ($1::regclass[])
)SQL";

/**
 * @brief CmdQueue::CmdQueue
 * @param result
 * @param row_number
 * @param field_numbers
 */
CmdQueue::CmdQueue(std::shared_ptr<PG::result> &result, int row_number, const std::unordered_map<std::string, int> &field_numbers) noexcept
{
    try
    {
        queue_cmd_relname = PQgetvalue(result->get(), row_number, field_numbers.at("queue_cmd_relname"));
        queue_signature_class = PQgetvalue(result->get(), row_number, field_numbers.at("queue_signature_class"));

        queue_runner_role = PQ::getnullable(result, row_number, field_numbers.at("queue_runner_role"));

        queue_notify_channel = PQ::getnullable(result, row_number, field_numbers.at("queue_notify_channel"));

        std::string queue_reselect_interval_msec = PQgetvalue(result->get(), row_number, field_numbers.at("queue_reselect_interval_msec"));
        this->queue_reselect_interval_msec = std::stoi(queue_reselect_interval_msec);

        if (not PQgetisnull(result->get(), row_number, field_numbers.at("queue_reselect_randomized_every_nth")))
        {
            std::string queue_reselect_randomized_every_nth = PQgetvalue(result->get(), row_number, field_numbers.at("queue_reselect_randomized_every_nth"));
            this->queue_reselect_randomized_every_nth = std::stoi(queue_reselect_randomized_every_nth);
        }

        ansi_fg = PQgetvalue(result->get(), row_number, field_numbers.at("ansi_fg"));

        _is_valid = true;
    }
    catch (std::exception &ex)
    {
        _validation_error_message = formatString("Error parsing cmd_queue metadata for `%s` queue: %s", this->queue_cmd_relname.c_str(), ex.what());
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
