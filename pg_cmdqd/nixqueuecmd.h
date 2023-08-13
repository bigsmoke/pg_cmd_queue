#ifndef NIXQUEUECMD_H
#define NIXQUEUECMD_H

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lwpg_nullable.h"
#include "lwpg_result.h"

using time_stamp = std::chrono::time_point<std::chrono::system_clock,
                                           std::chrono::microseconds>;

class NixQueueCmd
{
public:
    std::string queue_cmd_class;
    std::string queue_cmd_relname;
    std::string cmd_id;
    lwpg::nullable_string cmd_subid;

    // PostgreSQL's has a `to_timestamp(double) function which expects the subsecond digits as the decimal part.
    double cmd_queued_since;
    double cmd_runtime_start;
    double cmd_runtime_end;

    std::vector<std::string> cmd_argv;
    std::unordered_map<std::string, std::string> cmd_env;
    std::string cmd_stdin;
    lwpg::nullable_int cmd_term_sig;
    lwpg::nullable_int cmd_exit_code;
    std::string cmd_stdout = "";
    std::string cmd_stderr = "";

    bool _is_valid = false;

    NixQueueCmd() = default;
    NixQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    bool is_valid() const;
    void run_cmd();
};

#endif // NIXQUEUECMD_H
