#ifndef NIXQUEUECMD_H
#define NIXQUEUECMD_H

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"
#include "lwpg_nullable.h"
#include "lwpg_result.h"
#include "cmdqueue.h"
#include "queuecmdmetadata.h"

class NixQueueCmd
{
    Logger *logger = Logger::getInstance();

    static const std::string SELECT_STMT_WITHOUT_RELNAME;
    static const std::string UPDATE_STMT_WITHOUT_RELNAME;

    bool _is_valid = false;

    std::string cmd_line() const;
    bool cmd_succeeded() const;

public:
    QueueCmdMetadata meta;

    std::vector<std::string> cmd_argv;
    std::unordered_map<std::string, std::string> cmd_env;
    std::string cmd_stdin;
    std::optional<int> cmd_term_sig;
    std::optional<int> cmd_exit_code;
    std::string cmd_stdout = "";
    std::string cmd_stderr = "";

    NixQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    ~NixQueueCmd();

    static std::string select_stmt(const CmdQueue &cmd_queue);
    static std::string update_stmt(const CmdQueue &cmd_queue);
    std::vector<std::optional<std::string>> update_params();

    void run_cmd(std::shared_ptr<lwpg::Conn> &conn);
};

#endif // NIXQUEUECMD_H
