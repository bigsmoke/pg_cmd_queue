#include "cmdqueuerunner.h"

#include <functional>
#include <iostream>
#include <stdexcept>

#include <sys/epoll.h>
#include <unistd.h>

#include "utils.h"
#include "lwpg_context.h"
#include "nixqueuecmdcontext.h"
#include "nixqueuecmd.h"

#define MAX_PG_EPOLL_EVENTS 20


CmdQueueRunner::CmdQueueRunner(const CmdQueue &cmd_queue, const std::string &conn_str) :
    _cmd_queue(cmd_queue),
    _conn_str(conn_str)
{
    auto f = std::bind(&CmdQueueRunner::_run, this);
    thread = std::thread(f);
}

void CmdQueueRunner::_run()
{
    // From `man 2 epoll_create`: “the size argument is ignored but must be greater than zero”
    int epoll_fd = check<std::runtime_error>(epoll_create(69));
    struct epoll_event events[MAX_PG_EPOLL_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_PG_EPOLL_EVENTS);

    std::cout << "Runner thread " << _cmd_queue.queue_cmd_class << ": connecting to database…" << std::endl;

    lwpg::Context pg;
    pg.connectdb(_conn_str);

    NixQueueCmdContext nix_queue_cmd_context(pg.get_conn(), _cmd_queue);

    // TODO: Log session characteristics

    pg.exec("SET search_path TO cmdq");

    if (_cmd_queue.queue_runner_role)
    {
        std::cout << "Runner thread " << _cmd_queue.queue_cmd_class << ": Setting role to " << _cmd_queue.queue_runner_role.value() << std::endl;
        pg.exec("SET ROLE TO $1", {_cmd_queue.queue_runner_role.value()});
    }

    // TODO: LISTEN if NOTIFY channel has been specified

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pg.socket(), events);  // TODO: Check with Wiebe

    while (this->_keep_running)
    {
        pg.exec("BEGIN TRANSACTION");
        NixQueueCmd nix_queue_cmd = nix_queue_cmd_context.select_for_update();
        nix_queue_cmd.run_cmd();
        nix_queue_cmd_context.update(nix_queue_cmd);
        pg.exec("COMMIT TRANSACTION");

        int fd_count = epoll_wait(epoll_fd, events, MAX_PG_EPOLL_EVENTS, this->_cmd_queue.queue_reselect_interval_msec);
        if (fd_count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
        }
        std::cout << "Jippie!" << std::endl;
    }

    close(epoll_fd);  // TODO: Ask Wiebe: should this not go in the destructor somehow to guarantee `close()`?
}

void CmdQueueRunner::stop_running()
{
    this->_keep_running = false;
}
