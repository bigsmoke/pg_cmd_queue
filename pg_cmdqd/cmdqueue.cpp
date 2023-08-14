#include "cmdqueue.h"

#include <iostream>
#include <memory>

#include "lwpg_result.h"
#include "utils.h"

// TODO: Add _STMT suffix
const std::string CmdQueue::SELECT_STMT = R"SQL(
    SELECT
        (parse_ident(queue_cmd_class::regclass::text))[
            array_upper(parse_ident(queue_cmd_class::regclass::text), 1)
        ] as queue_cmd_class
        ,(parse_ident(queue_signature_class::regclass::text))[
            array_upper(parse_ident(queue_signature_class::regclass::text), 1)
        ] as queue_signature_class
        ,queue_runner_euid
        ,queue_runner_egid
        ,queue_runner_role::text
        ,queue_notify_channel
        ,extract('epoch' from queue_reselect_interval) * 10^3 as queue_reselect_interval_msec
    FROM
        cmdq.cmd_queue
)SQL";

/**
 * @brief CmdQueue::CmdQueue
 * @param result
 * @param row
 * @param fieldMapping
 */
CmdQueue::CmdQueue(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept
{
    try
    {
        queue_cmd_class = PQgetvalue(result->get(), row, fieldMapping.at("queue_cmd_class"));
        queue_signature_class = PQgetvalue(result->get(), row, fieldMapping.at("queue_signature_class"));

        if (not PQgetisnull(result->get(), row, fieldMapping.at("queue_runner_euid")))
        {
            std::string queue_runner_euid = PQgetvalue(result->get(), row, fieldMapping.at("queue_runner_euid"));
            this->queue_runner_euid = std::stoi(queue_runner_euid);
        }

        if (not PQgetisnull(result->get(), row, fieldMapping.at("queue_runner_egid")))
        {
            std::string queue_runner_egid = PQgetvalue(result->get(), row, fieldMapping.at("queue_runner_egid"));
            this->queue_runner_egid = std::stoi(queue_runner_egid);
        }

        queue_runner_role = lwpg::getnullable(result->get(), row, fieldMapping.at("queue_runner_role"));

        queue_notify_channel = lwpg::getnullable(result->get(), row, fieldMapping.at("queue_notify_channel"));

        std::string queue_reselect_interval_msec = PQgetvalue(result->get(), row, fieldMapping.at("queue_reselect_interval_msec"));
        this->queue_reselect_interval_msec = std::stoi(queue_reselect_interval_msec);

        _is_valid = true;
    }
    catch (std::exception &ex)
    {
        std::cerr << "Error parsing user data for '" << this->queue_cmd_class.c_str() << "': " << ex.what();
        _is_valid = false;
    }
}

bool CmdQueue::is_valid() const
{
    return this->_is_valid;
}
