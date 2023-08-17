#ifndef CMDQUEUERUNNER_H
#define CMDQUEUERUNNER_H

#include <thread>

#include "lwpg_context.h"
#include "cmdqueue.h"
#include "logger.h"

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

#endif // CMDQUEUERUNNER_H
