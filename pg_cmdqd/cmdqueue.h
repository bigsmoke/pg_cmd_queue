#ifndef CMDQUEUE_H
#define CMDQUEUE_H

#include <memory>
#include <string>
#include <unordered_map>

#include "pq-raii/libpq-raii.hpp"

class CmdQueue
{
    bool _is_valid = false;
    std::string _validation_error_message;

public:
    static const std::string CMD_QUEUE_RELNAME;
    static const std::string SELECT_STMT;

    std::string queue_cmd_relname;

    /*
     * The local part of the queue's template table name.
     */
    std::string queue_signature_class;

    std::optional<std::string> queue_runner_role;
    std::optional<std::string> queue_notify_channel;
    int queue_reselect_interval_msec;
    std::optional<int> queue_reselect_randomized_every_nth;
    double queue_cmd_timeout_sec;
    std::string ansi_fg;

    CmdQueue() = default;
    CmdQueue(std::shared_ptr<PG::result> &result, int row_number, const std::unordered_map<std::string, int> &field_numbers) noexcept;
    bool is_valid() const;
    std::string validation_error_message() const;
};

#endif  // CMDQUEUE_H
