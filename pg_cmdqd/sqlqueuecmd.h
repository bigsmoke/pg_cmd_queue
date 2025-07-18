#ifndef SQLQUEUECMD_H
#define SQLQUEUECMD_H

#include <string>

#include "pq-raii/libpq-raii.hpp"
#include "cmdqueue.h"
#include "logger.h"
#include "queuecmdmetadata.h"


class SqlQueueCmd
{
    Logger *logger = Logger::getInstance();

    bool _is_valid = false;

    static void receive_notice_c_wrapper(void *arg, const PGresult *res);
    void receive_notice(const PGresult *res);

    std::optional<std::map<char, std::optional<std::string>>> handle_sql_fatality(
            const PG::result &result);

public:
    QueueCmdMetadata meta;

    std::string cmd_sql;
    std::string cmd_sql_result_status;
    std::vector<std::vector<std::string>> cmd_sql_result_rows;
    std::optional<std::map<char, std::optional<std::string>>> cmd_sql_fatal_error;
    std::vector<std::map<char, std::optional<std::string>>> cmd_sql_nonfatal_errors;

    SqlQueueCmd() = delete;
    SqlQueueCmd(const PG::result &result,
                int row,
                const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    ~SqlQueueCmd() = default;

    bool is_valid() const;

    std::vector<std::optional<std::string>> update_params() const;
    std::vector<int> update_param_lengths() const;
    std::vector<int> update_param_formats() const;

    void run_cmd(std::shared_ptr<PG::conn> &conn, const double queue_cmd_timeout_sec);
};

#endif // SQLQUEUECMD_H
