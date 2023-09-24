#ifndef CMDQUEUERUNNERMANAGER_H
#define CMDQUEUERUNNERMANAGER_H

#include <shared_mutex>

#include "cmdqueuerunner.h"
#include "logger.h"

/**
 * Because the world isn't waiting for yet another `…Manager` class, I sneakily called it `…Collection`.
 */
class CmdQueueRunnerCollection
{
    mutable std::shared_mutex _mutex;
    std::unordered_map<std::string, CmdQueueRunner<NixQueueCmd>> _nix_cmd_queue_runners;
    std::unordered_map<std::string, CmdQueueRunner<SqlQueueCmd>> _sql_cmd_queue_runners;
    Logger *logger = Logger::getInstance();
    std::string _conn_str;
    std::shared_ptr<lwpg::Conn> _conn;

public:
    CmdQueueRunnerCollection() = delete;
    CmdQueueRunnerCollection(const std::string &conn_str);
    void refresh_queue_list(const std::vector<std::string> &explicit_queue_cmd_classes);
    void listen_for_queue_list_changes();
    void add_runner(const CmdQueue &cmd_queue);
    void join_all_threads();
    std::vector<std::string> queue_cmd_classes();
};

#endif // CMDQUEUERUNNERMANAGER_H
