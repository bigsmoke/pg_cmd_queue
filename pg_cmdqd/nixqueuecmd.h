#ifndef NIXQUEUECMD_H
#define NIXQUEUECMD_H

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"
#include "lwpg_nullable.h"
#include "lwpg_result.h"
#include "cmdqueue.h"

using time_stamp = std::chrono::time_point<std::chrono::system_clock,
                                           std::chrono::microseconds>;

class NixQueueCmd
{
    static const std::string SELECT_STMT_WITHOUT_RELNAME;
    static const std::string UPDATE_STMT_WITHOUT_RELNAME;
    Logger *logger = Logger::getInstance();

public:
    std::string queue_cmd_class;
    std::string queue_cmd_relname;
    std::string cmd_id;
    std::optional<std::string> cmd_subid;

    // PostgreSQL's has a `to_timestamp(double) function which expects the subsecond digits as the decimal part.
    double cmd_queued_since;
    double cmd_runtime_start;
    double cmd_runtime_end;

    std::vector<std::string> cmd_argv;
    std::unordered_map<std::string, std::string> cmd_env;
    std::string cmd_stdin;
    std::optional<int> cmd_term_sig;
    std::optional<int> cmd_exit_code;
    std::string cmd_stdout = "";
    std::string cmd_stderr = "";

    bool _is_valid = false;

    NixQueueCmd() = default;
    NixQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    bool is_valid() const;

    static std::string select_stmt(const CmdQueue &cmd_queue);
    std::string update_stmt(const CmdQueue &cmd_queue) const;
    std::vector<std::optional<std::string>> update_params() const;

    std::string cmd_line() const;
    bool cmd_succeeded() const;
    void run_cmd();
};

#endif // NIXQUEUECMD_H
