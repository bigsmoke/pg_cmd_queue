#ifndef CMDQUEUERUNNERMANAGER_H
#define CMDQUEUERUNNERMANAGER_H

#include <set>
#include <shared_mutex>

#include "pq-raii/libpq-raii.hpp"
#include "cmdqueuerunner.h"
#include "logger.h"

class CmdQueueRunnerManager
{
    std::unordered_map<std::string, CmdQueueRunner<NixQueueCmd>> _nix_cmd_queue_runners;
    std::unordered_map<std::string, CmdQueueRunner<SqlQueueCmd>> _sql_cmd_queue_runners;
    std::set<std::string> _old_queue_cmd_classes;
    std::set<std::string> _new_queue_cmd_classes;
    Logger *logger = Logger::getInstance();
    std::string _conn_str;
    std::shared_ptr<PG::conn> _conn;
    bool _keep_running = true;
    sigset_t _sigset_masked_in_runner_threads;
    PipeFds _kill_pipe_fds;

public:
    bool emit_sigusr1_when_ready = false;
    bool _emitted_sigusr1_yet = false;
    std::vector<std::string> explicit_queue_cmd_classes;

    CmdQueueRunnerManager() = delete;
    CmdQueueRunnerManager(
            const std::string &conn_str,
            const bool emit_sigusr1_when_ready = false,
            const std::vector<std::string> &explicit_queue_cmd_classes = {});
    bool queue_has_runner_already(const CmdQueue &cmd_queue);
    void refresh_queue_list();  // TODO: Check if changed queues are adequately restarted
    void listen_for_queue_list_changes();
    void add_runner(const CmdQueue &cmd_queue);
    void stop_runner(const std::string &queue_cmd_class, const int simulate_signal);
    void stop_all_runners();
    void join_all_threads();
    std::vector<std::string> queue_cmd_classes();
    void receive_signal(const int sig_num);
    void install_signal_handlers();
};

extern std::function<void(int)> cpp_signal_handler;

void c_signal_handler(int value);

#endif // CMDQUEUERUNNERMANAGER_H
