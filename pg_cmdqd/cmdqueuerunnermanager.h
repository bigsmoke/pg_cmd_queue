#ifndef CMDQUEUERUNNERMANAGER_H
#define CMDQUEUERUNNERMANAGER_H

#include <set>
#include <shared_mutex>

#include "pq-raii/libpq-raii.hpp"
#include "cmdqueuerunner.h"
#include "logger.h"

class CmdQueueRunnerManager
{
    mutable std::shared_mutex _mutex;
    std::unordered_map<std::string, CmdQueueRunner<NixQueueCmd>> _nix_cmd_queue_runners;
    std::unordered_map<std::string, CmdQueueRunner<SqlQueueCmd>> _sql_cmd_queue_runners;
    std::set<std::string> _old_queue_cmd_classes;
    std::set<std::string> _new_queue_cmd_classes;
    Logger *logger = Logger::getInstance();
    std::string _conn_str;
    std::shared_ptr<PG::conn> _conn;

public:
    std::vector<std::string> explicit_queue_cmd_classes;
    CmdQueueRunnerManager() = delete;
    CmdQueueRunnerManager(const std::string &conn_str, const std::vector<std::string> &explicit_queue_cmd_classes = {});
    bool queue_has_runner_already(const CmdQueue &cmd_queue);
    void refresh_queue_list();
    void listen_for_queue_list_changes();
    void add_runner(const CmdQueue &cmd_queue);
    void stop_runner(const std::string &queue_cmd_class);
    void join_all_threads();
    std::vector<std::string> queue_cmd_classes();
};

#endif // CMDQUEUERUNNERMANAGER_H
