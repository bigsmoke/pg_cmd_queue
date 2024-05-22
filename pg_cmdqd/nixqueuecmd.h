#ifndef NIXQUEUECMD_H
#define NIXQUEUECMD_H

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "pq-raii/libpq-raii.hpp"
#include "logger.h"
#include "cmdqueue.h"
#include "queuecmdmetadata.h"

class NixQueueCmd
{
    Logger *logger = Logger::getInstance();

    bool _is_valid = false;

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

    NixQueueCmd(
            std::shared_ptr<PG::result> &result,
            int row,
            const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    NixQueueCmd(
            const std::string &cmd_class_identity,
            const std::string &cmd_class_relname,
            const std::string &cmd_id,
            const std::optional<std::string> &cmd_subid,
            const std::vector<std::string> &cmd_argv,
            const std::unordered_map<std::string, std::string> &cmd_env,
            const std::string &cmd_stdin
        );
    ~NixQueueCmd();

    std::string update_stmt(const std::shared_ptr<PG::conn> &conn);

    std::vector<std::optional<std::string>> update_params() const;
    std::vector<int> update_param_lengths() const;
    std::vector<int> update_param_formats() const;

    std::string cmd_line() const;

    void run_cmd(std::shared_ptr<PG::conn> &conn, const double queue_cmd_timeout);
};

#endif // NIXQUEUECMD_H
