#ifndef CMDQUEUE_H
#define CMDQUEUE_H

#include <memory>
#include <string>
#include <unordered_map>

#include "lwpg_nullable.h"
#include "lwpg_result.h"

class CmdQueue
{
public:
    static const std::string SELECT;

    std::string queue_cmd_class;
    std::string queue_signature_class;
    int queue_runner_euid;
    int queue_runner_egid;
    lwpg::nullable_string queue_runner_role;
    lwpg::nullable_string queue_notify_channel;
    int queue_reselect_interval_usec;
    bool _is_valid = false;

    CmdQueue() = default;
    CmdQueue(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    bool is_valid() const;
};

#endif  // CMDQUEUE_H
