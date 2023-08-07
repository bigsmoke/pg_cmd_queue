#ifndef NIXQUEUECMD_H
#define NIXQUEUECMD_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lwpg_result.h"

struct NixQueueCmd
{
    static const std::string SELECT_STMT_WITHOUT_RELNAME;

    std::string queue_cmd_class;
    std::string cmd_id;
    std::string cmd_subid;
    int cmd_queued_since;
    int cmd_runtime_start;
    int cmd_runtime_end;
    std::vector<std::string> cmd_argv;
    std::unordered_map<std::string, std::string> cmd_env;
    std::string cmd_stdin;
    int cmd_exit_code;
    std::string cmd_stdout;
    std::string cmd_stderr;

    bool _is_valid = false;

public:
    NixQueueCmd() = default;
    NixQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    bool is_valid() const;
    static std::string select(std::string& local_relname);
};

#endif // NIXQUEUECMD_H
