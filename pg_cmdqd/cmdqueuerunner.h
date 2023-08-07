#ifndef CMDQUEUERUNNER_H
#define CMDQUEUERUNNER_H

#define MAX_EVENTS 20

#include <thread>

#include "cmdqueue.h"

class CmdQueueRunner
{
    bool _keep_running = true;
    CmdQueue _cmd_queue;

    void run();

public:
    std::thread thread;

    CmdQueueRunner() = delete;
    CmdQueueRunner(const CmdQueue &cmd_queue);
    ~CmdQueueRunner() = default;

    void stop_running();
};

#endif // CMDQUEUERUNNER_H
