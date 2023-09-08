#include "cmdqueuerunner.h"

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <sys/epoll.h>
#include <unistd.h>

#include "utils.h"
#include "logger.h"
#include "lwpg_context.h"
#include "nixqueuecmd.h"

#define MAX_PG_EPOLL_EVENTS 20

template <typename T>
CmdQueueRunner<T>::CmdQueueRunner(const CmdQueue &cmd_queue, const std::string &conn_str) :
    _cmd_queue(cmd_queue),
    _conn_str(conn_str)
{
    auto f = std::bind(&CmdQueueRunner<T>::_run, this);
    thread = std::thread(f);
}

template <typename T>
void CmdQueueRunner<T>::_run()
{
    Logger::cmd_queue = std::make_shared<CmdQueue>(_cmd_queue);

    // From `man 2 epoll_create`: “the size argument is ignored but must be greater than zero”
    int epoll_fd = check<std::runtime_error>(epoll_create(69));
    struct epoll_event events[MAX_PG_EPOLL_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_PG_EPOLL_EVENTS);

    logger->log(LOG_INFO, "Connecting to database…");

    lwpg::Context pg;
    std::shared_ptr<lwpg::Conn> conn = pg.connectdb(_conn_str);

    // TODO: Log session characteristics

    pg.exec("SET search_path TO cmdq");

    if (_cmd_queue.queue_runner_role)
    {
        logger->log(LOG_INFO, "Setting role to \x1b[1m%s\x1b[22m", _cmd_queue.queue_runner_role.value().c_str());
        pg.exec(formatString(
            "SET ROLE TO %s",
            PQescapeIdentifier(
                conn->get(),
                _cmd_queue.queue_runner_role.value().c_str(),
                _cmd_queue.queue_runner_role.value().length()
            )
        ));
    }

    // TODO: LISTEN if NOTIFY channel has been specified

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pg.socket(), events);  // TODO: Check with Wiebe

    pg.exec("BEGIN TRANSACTION");

    while (this->_keep_running)
    {
        logger->log(LOG_DEBUG3, "Checking for item in queue…");
        std::optional<T> potential_queue_cmd = pg.query1<T>(T::select_stmt(_cmd_queue));
        if (potential_queue_cmd.has_value())
        {
            T queue_cmd = std::move(potential_queue_cmd.value());

            queue_cmd.run_cmd(conn);

            try
            {
                pg.exec(queue_cmd.update_stmt(_cmd_queue), queue_cmd.update_params());  // TODO: Drop argument; we should have it from the SELECT
                pg.exec("COMMIT TRANSACTION AND CHAIN");
            }
            catch (const std::runtime_error &err)
            {
                logger->log(
                    LOG_ERROR, "SQL UPDATE for command %s failed: %s",
                    queue_cmd.meta.cmd_id.c_str(), err.what()
                );
                pg.exec("ROLLBACK TRANSACTION AND CHAIN");
            }
        }

        int fd_count = epoll_wait(epoll_fd, events, MAX_PG_EPOLL_EVENTS, _cmd_queue.queue_reselect_interval_msec);
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

template <typename T>
void CmdQueueRunner<T>::stop_running()
{
    this->_keep_running = false;
}
