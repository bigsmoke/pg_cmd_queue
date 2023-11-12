#include "cmdqueue.h"

#include <iostream>
#include <memory>

#include "pq-raii/libpq-raii.hpp"
#include "utils.h"

const std::string CmdQueue::CMD_QUEUE_RELNAME = "cmd_queue";

const std::string CmdQueue::SELECT_STMT = R"SQL(
    SELECT
        r.relname AS queue_cmd_relname
        ,r.relnamespace::regnamespace::text || '.' || quote_ident(r.relname) AS queue_cmd_class_qualified
        ,(parse_ident(q.queue_signature_class::regclass::text))[
            array_upper(parse_ident(q.queue_signature_class::regclass::text), 1)
        ] AS queue_signature_class
        ,q.queue_runner_role::text
        ,q.queue_notify_channel
        ,extract('epoch' from q.queue_reselect_interval) * 10^3 AS queue_reselect_interval_msec
        ,q.queue_reselect_randomized_every_nth
        ,extract('epoch' from q.queue_cmd_timeout) AS queue_cmd_timeout_sec
        ,color.ansi_fg
    FROM
        cmdq.cmd_queue as q
    INNER JOIN
        pg_catalog.pg_class as r
        ON r.oid = q.queue_cmd_class
    CROSS JOIN LATERAL
        cmdq.queue_cmd_class_color(queue_cmd_class) as color
    WHERE
        (NULLIF($1, '{}'::text[]) IS NULL OR queue_cmd_class = ANY ($1::regclass[]))
        AND queue_is_enabled
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
        queue_cmd_class_qualified = PQgetvalue(result->get(),
                                               row_number,
                                               field_numbers.at("queue_cmd_class_qualified"));
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

        if (not PQ::getisnull(result, row_number, field_numbers.at("queue_cmd_timeout_sec")))
        {
            std::string queue_cmd_timeout_sec = PQgetvalue(result->get(), row_number, field_numbers.at("queue_cmd_timeout_sec"));
            this->queue_cmd_timeout_sec = std::stod(queue_cmd_timeout_sec);
        }
        else
            this->queue_cmd_timeout_sec = 0;

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
