#ifndef CMDQUEUERUNNERMANAGER_H
#define CMDQUEUERUNNERMANAGER_H

#include <set>
#include <shared_mutex>

#include "pq-raii/libpq-raii.hpp"
#include "cmdqueuerunner.h"
#include "logger.h"

class CmdQueueRunnerManager
{
    static inline CmdQueueRunnerManager* _instance = nullptr;

    std::unordered_map<std::string, CmdQueueRunner<NixQueueCmd>> _nix_cmd_queue_runners;
    std::unordered_map<std::string, CmdQueueRunner<SqlQueueCmd>> _sql_cmd_queue_runners;
    std::set<std::string> _old_cmd_classes;
    std::set<std::string> _new_cmd_classes;
    Logger *logger = Logger::getInstance();
    std::string _conn_str;
    std::shared_ptr<PG::conn> _conn;
    bool _keep_running = true;
    bool _emitted_sigusr1_yet = false;
    sigset_t _sigset_masked_in_runner_threads;
    PipeFds _kill_pipe_fds;

    CmdQueueRunnerManager() = delete;
    CmdQueueRunnerManager(
            const std::string &conn_str,
            const bool emit_sigusr1_when_ready,
            const std::vector<std::string> &explicit_cmd_classes);

public:
    bool emit_sigusr1_when_ready = false;
    std::vector<std::string> explicit_cmd_classes;

    static CmdQueueRunnerManager *get_instance();
    static CmdQueueRunnerManager *make_instance(
            const std::string &conn_str,
            const bool emit_sigusr1_when_ready = false,
            const std::vector<std::string> &explicit_cmd_classes = {});
    bool queue_has_runner_already(const CmdQueue &cmd_queue);
    void refresh_queue_list(const bool retry_select);
    void listen_for_queue_list_changes();
    void add_runner(const CmdQueue &cmd_queue);
    void stop_runner(const std::string &cmd_class, const int simulate_signal);
    void stop_all_runners();
    void join_all_threads();
    std::vector<std::string> cmd_classes();
    void receive_signal(const int sig_num);
    void install_signal_handlers();
    void maintain_connection(bool one_shot=false);
    std::vector<std::string> get_cmd_class_names();
};

#endif // CMDQUEUERUNNERMANAGER_H
