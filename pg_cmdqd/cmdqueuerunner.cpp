#include "cmdqueuerunner.h"

#include <functional>
#include <iostream>
#include <stdexcept>

#include <sys/epoll.h>

#include "utils.h"
#include "lwpg_context.h"

CmdQueueRunner::CmdQueueRunner(const CmdQueue &cmd_queue, const std::string &conn_str) :
    _cmd_queue(cmd_queue),
    _conn_str(conn_str)
{
    auto f = std::bind(&CmdQueueRunner::_run, this);
    thread = std::thread(f);
}

void CmdQueueRunner::_run()
{
    int epoll_fd = check<std::runtime_error>(epoll_create(69));
    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    std::cout << "Runner thread " << _cmd_queue.queue_cmd_class << ": connecting to database…" << std::endl;

    lwpg::Context pg;
    pg.connectdb(_conn_str);

    // TODO: Log session characteristics

    if (_cmd_queue.queue_runner_role)
    {
        std::cout << "Runner thread " << _cmd_queue.queue_cmd_class << ": Setting role to " << _cmd_queue.queue_runner_role.value() << std::endl;
        pg.exec("SET ROLE TO $1", {_cmd_queue.queue_runner_role.value()});
    }

    while (this->_keep_running)
    {
        int fd_count = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (fd_count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
        }
        std::cout << "Jippie!" << std::endl;
    }
}

void CmdQueueRunner::stop_running()
{
    this->_keep_running = false;
}
