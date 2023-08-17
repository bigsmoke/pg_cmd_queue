#include "cmdqueuerunner.h"

#include <functional>
#include <iostream>
#include <stdexcept>

#include <sys/epoll.h>
#include <unistd.h>

#include "utils.h"
#include "logger.h"
#include "lwpg_context.h"
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

    logger->log(
        LOG_INFO,
        "Runner thread \x1b[1m%s\x1b[0m: connecting to database…",
        _cmd_queue.queue_cmd_relname.c_str()
    );

    lwpg::Context pg;
    pg.connectdb(_conn_str);

    // TODO: Log session characteristics

    pg.exec("SET search_path TO cmdq");

    if (_cmd_queue.queue_runner_role)
    {
        logger->log(
            LOG_INFO,
            "Runner thread \x1b[1m%s\x1b[0m: Setting role to \x1b[1m%s\x1b[0m",
            _cmd_queue.queue_cmd_relname.c_str(),
            _cmd_queue.queue_runner_role.value().c_str()
        );
        pg.exec("SET ROLE TO $1", {_cmd_queue.queue_runner_role.value()});
    }

    // TODO: LISTEN if NOTIFY channel has been specified

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pg.socket(), events);  // TODO: Check with Wiebe

    pg.exec("BEGIN TRANSACTION");

    while (this->_keep_running)
    {
        std::optional<NixQueueCmd> nix_queue_cmd;

        logger->log(
            LOG_DEBUG3,
            "Runner thread \x1b[1m%s\x1b[0m: Checking for item in queue…",
            _cmd_queue.queue_cmd_relname.c_str()
        );
        if ((nix_queue_cmd = pg.query1<NixQueueCmd>(NixQueueCmd::select_stmt(_cmd_queue))))
        {
            nix_queue_cmd.value().run_cmd();

            try
            {
                pg.exec(nix_queue_cmd.value().update_stmt(_cmd_queue), nix_queue_cmd.value().update_params());  // TODO: Drop argument; we should have it from the SELECT
                pg.exec("COMMIT TRANSACTION AND CHAIN");
            }
            catch (const std::runtime_error &err)
            {
                logger->log(LOG_ERROR, "SQL UPDATE for command %s failed: %s", nix_queue_cmd.value().cmd_id.c_str(), err.what());
                pg.exec("ROLLBACK TRANSACTION AND CHAIN");
            }
        }

        int fd_count = epoll_wait(epoll_fd, events, MAX_PG_EPOLL_EVENTS, this->_cmd_queue.queue_reselect_interval_msec);
        if (fd_count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
        }
    }

    pg.exec("ROLLBACK TRANSACTION");

    close(epoll_fd);  // TODO: Ask Wiebe: should this not go in the destructor somehow to guarantee `close()`?
}

void CmdQueueRunner::stop_running()
{
    this->_keep_running = false;
}
