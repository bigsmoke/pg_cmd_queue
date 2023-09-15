#ifndef CMDQUEUERUNNER_H
#define CMDQUEUERUNNER_H

#include <functional>
#include <thread>

#include "lwpg_context.h"
#include "cmdqueue.h"
#include "logger.h"
#include "nixqueuecmd.h"
#include "sqlqueuecmd.h"

template <typename T>
class CmdQueueRunner
{
    bool _keep_running = true;
    CmdQueue _cmd_queue;
    std::string _conn_str;
    Logger *logger = Logger::getInstance();

    void _run();

public:
    std::thread thread;

    CmdQueueRunner() = delete;
    CmdQueueRunner(const CmdQueue &cmd_queue, const std::string &conn_str);
    ~CmdQueueRunner() = default;

    void stop_running();
};

template class CmdQueueRunner<NixQueueCmd>;
template class CmdQueueRunner<SqlQueueCmd>;

// Strictly speaking, the above template “speciation” declarations should allow the below to be in
// a regular .cpp file, but for some reason this blew up on Macos.
#include "cmdqueuerunner.tpp"

#endif // CMDQUEUERUNNER_H
