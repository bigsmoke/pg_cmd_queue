#ifndef MAINSTATE_H
#define MAINSTATE_H

#include <mutex>
#include <thread>
#include <unordered_map>

#include "lwpg_context.h"
#include "cmdqueue.h"

class MainState
{
    std::mutex refreshMutex;

    std::string getConnectionString() const;

public:
    std::unordered_map<std::string, CmdQueue> newQueues;
    std::unordered_map<std::string, CmdQueue> oldQueues;
    std::unordered_map<std::string, CmdQueue> queues;
    std::unordered_map<std::string, std::thread> threads;

    MainState();
    ~MainState();

    void refresh();
};

#endif // MAINSTATE_H
