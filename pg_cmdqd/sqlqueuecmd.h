#ifndef SQLQUEUECMD_H
#define SQLQUEUECMD_H

#include <string>

#include "cmdqueue.h"
#include "logger.h"
#include "lwpg_conn.h"
#include "lwpg_error.h"
#include "queuecmdmetadata.h"


class SqlQueueCmd
{
    static const std::string SELECT_STMT_WITHOUT_RELNAME;
    static const std::string UPDATE_STMT_WITHOUT_RELNAME;

    Logger *logger = Logger::getInstance();

    bool _is_valid = false;

    static void receive_notice_c_wrapper(void *arg, const PGresult *res);
    void receive_notice(const PGresult *res);

    lwpg::Error handle_sql_fatality(std::shared_ptr<lwpg::Result> &result);

public:
    QueueCmdMetadata meta;

    std::string cmd_sql;
    std::string cmd_sql_result_status;
    std::vector<std::vector<std::string>> cmd_sql_result_rows;
    std::optional<lwpg::Error> cmd_sql_fatal_error;
    std::vector<lwpg::Error> cmd_sql_nonfatal_errors;

    SqlQueueCmd() = delete;
    SqlQueueCmd(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    ~SqlQueueCmd() = default;

    bool is_valid() const;

    static std::string select_stmt(const CmdQueue &cmd_queue);
    static std::string update_stmt(const CmdQueue &cmd_queue);
    std::vector<std::optional<std::string>> update_params();

    void run_cmd(std::shared_ptr<lwpg::Conn> &conn);
};

#endif // SQLQUEUECMD_H
