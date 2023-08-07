#include "cmdqueuerunner.h"

#include <functional>
#include <iostream>
#include <stdexcept>

#include <sys/epoll.h>

#include "utils.h"

CmdQueueRunner::CmdQueueRunner(const CmdQueue &cmd_queue) :
    _cmd_queue(cmd_queue)
{
    auto f = std::bind(&CmdQueueRunner::run, this);
    thread = std::thread(f);
}

void CmdQueueRunner::run()
{
    int epoll_fd = check<std::runtime_error>(epoll_create(69));
    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    while (this->_keep_running)
    {
        int fd_count = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (fd_count < 0)
        {
            if (errno == EINTR)
                continue;
        }
        std::cout << "Jippie!" << std::endl;
    }
}

void CmdQueueRunner::stop_running()
{
    this->_keep_running = false;
}
