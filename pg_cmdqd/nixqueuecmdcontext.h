#ifndef NIXQUEUECMDCONTEXT_H
#define NIXQUEUECMDCONTEXT_H

#include <string>

#include "lwpg_conn.h"

#include "cmdqueue.h"
#include "nixqueuecmd.h"

class NixQueueCmdContext
{
    static const std::string SELECT_STMT_WITHOUT_RELNAME;
    static const std::string UPDATE_STMT_WITHOUT_RELNAME;
    static const std::unordered_map<std::string, int> SELECT_FIELD_MAPPING;
    std::shared_ptr<lwpg::Conn> _conn;
    CmdQueue _cmd_queue;

    std::string select_stmt();
    std::string update_stmt();

public:
    NixQueueCmdContext(std::shared_ptr<lwpg::Conn> conn, const CmdQueue &cmd_queue);

    NixQueueCmd select_for_update();
    void update(const NixQueueCmd &nix_queue_cmd);
};

#endif // NIXQUEUECMDCONTEXT_H
