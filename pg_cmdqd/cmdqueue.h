#ifndef CMDQUEUE_H
#define CMDQUEUE_H

#include <memory>
#include <string>
#include <unordered_map>

#include "lwpg_nullable.h"
#include "lwpg_result.h"

class CmdQueue
{
    bool _is_valid = false;
    std::string _validation_error_message;

public:
    static const std::string SELECT_STMT;

    std::string queue_cmd_relname;
    std::string queue_signature_class;
    int queue_runner_euid;
    int queue_runner_egid;
    std::optional<std::string> queue_runner_role;
    std::optional<std::string> queue_notify_channel;
    int queue_reselect_interval_msec;

    CmdQueue() = default;
    CmdQueue(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    bool is_valid() const;
    std::string validation_error_message() const;
};

#endif  // CMDQUEUE_H
