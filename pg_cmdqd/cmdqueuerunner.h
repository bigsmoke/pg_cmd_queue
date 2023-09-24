#ifndef CMDQUEUERUNNER_H
#define CMDQUEUERUNNER_H

#include <functional>
#include <thread>

#include "poll.h"

#include "lwpg_context.h"
#include "lwpg_result.h"
#include "cmdqueue.h"
#include "logger.h"
#include "nixqueuecmd.h"
#include "sqlqueuecmd.h"
#include "utils.h"

template <typename T>
class CmdQueueRunner
{
    bool _keep_running = true;
    CmdQueue _cmd_queue;
    std::string _conn_str;
    Logger *logger = Logger::getInstance();

    void _run()
    {
        Logger::cmd_queue = std::make_shared<CmdQueue>(_cmd_queue);


        if (not _conn_str.empty())
            logger->log(LOG_INFO, "Connecting to database: \x1b[1m%s\x1b[22m", _conn_str.c_str());
        else
            logger->log(LOG_DEBUG1, "No connectiong string given; letting libpq figure out what to do from the \x1b[1mPG*\x1b[22m environment variables…");

        std::shared_ptr<lwpg::Conn> conn = lwpg::connectdb(_conn_str);

        logger->log(
            LOG_INFO,
            "DB connection established to \x1b[1m%s\x1b[22m on \x1b[1m%s:%s\x1b[22m as \x1b[1m%s\x1b[22m",
            PQdb(conn->get()), PQhost(conn->get()), PQport(conn->get()), PQuser(conn->get())
        );

        lwpg::exec(conn, "SET search_path TO cmdq");

        if (_cmd_queue.queue_runner_role)
        {
            logger->log(LOG_INFO, "Setting role to \x1b[1m%s\x1b[22m", _cmd_queue.queue_runner_role.value().c_str());
            lwpg::exec(conn, formatString(
                "SET ROLE TO %s",
                PQescapeIdentifier(
                    conn->get(),
                    _cmd_queue.queue_runner_role.value().c_str(),
                    _cmd_queue.queue_runner_role.value().length()
                )
            ));
        }

        if (_cmd_queue.queue_notify_channel)
        {
            // Listen:
            lwpg::exec(conn, std::string("LISTEN \"") + _cmd_queue.queue_notify_channel.value() + "\"");
            // And lets the world know that we're listening:
            lwpg::execParams(
                conn,
                "SELECT pg_notify(pg_cmd_queue_notify_channel(), row($1, $2, $3)::text)",
                3, {}, {CmdQueue::CMD_QUEUE_RELNAME, _cmd_queue.queue_cmd_relname, "LISTEN"}
            );
        }

        lwpg::prepare(conn, "select_oldest", T::select_oldest(_cmd_queue));
        //pg.prepare("select_notify", T::select_notify(_cmd_queue));
        lwpg::prepare(conn, "select_random", T::select_random(_cmd_queue));

        // The field mappings are the same for all the `SELECT` statements of the same `T`.
        std::unordered_map<std::string, int> select_field_mappings = [conn]()
        {
            std::shared_ptr<lwpg::Result> result = lwpg::describePrepared(conn, "select_oldest");
            return lwpg::fnumbers(result);
        }();

        // Let the listener(s) know that we're about to start selecting things from the queue
        lwpg::execParams(
            conn,
            "SELECT pg_notify(pg_cmd_queue_notify_channel(), row($1, $2, $3)::text)",
            3, {}, {CmdQueue::CMD_QUEUE_RELNAME, _cmd_queue.queue_cmd_relname, "PREPARE"}
        );

        struct pollfd fds[] = {
            { PQsocket(conn->get()), POLLIN | POLLPRI | POLLOUT | POLLERR, 0 }
        };

        int reselect_found_count = 0;
        while (this->_keep_running)
        {
            reselect_found_count++;

            logger->log(LOG_DEBUG3, "Checking for item in queue…");

            lwpg::exec(conn, "BEGIN TRANSACTION");

            std::shared_ptr<lwpg::Result> result;
            if (_cmd_queue.queue_reselect_randomized_every_nth
                    and reselect_found_count % _cmd_queue.queue_reselect_randomized_every_nth.value() == 0)
                result = lwpg::execPrepared(conn, "select_random");
            else
                result = lwpg::execPrepared(conn, "select_oldest");

            if (result->getResultStatus() != PGRES_TUPLES_OK)
            {
                logger->log(LOG_ERROR, "Retrieving command from queue failed: %s", PQerrorMessage(conn->get()));
                lwpg::exec(conn, "ROLLBACK TRANSACTION");
            }
            else if (PQntuples(result->get()) < 1)
            {
                T queue_cmd(result, 0, select_field_mappings);

                queue_cmd.run_cmd(conn);

                try
                {
                    lwpg::execParams(conn, queue_cmd.update_stmt(_cmd_queue), queue_cmd.update_params().size(), {}, queue_cmd.update_params());
                    lwpg::exec(conn, "COMMIT TRANSACTION");
                }
                catch (const std::runtime_error &err)
                {
                    logger->log(
                        LOG_ERROR, "SQL UPDATE for command %s failed: %s",
                        queue_cmd.meta.cmd_id.c_str(), err.what()
                    );
                    lwpg::exec(conn, "ROLLBACK TRANSACTION");
                }
            }

            int fd_count = poll(fds, 1, _cmd_queue.queue_reselect_interval_msec);
            if (fd_count < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
            }
        }
    }

public:
    std::thread thread;

    CmdQueueRunner() = delete;

    CmdQueueRunner(const CmdQueue &cmd_queue, const std::string &conn_str) :
        _cmd_queue(cmd_queue),
        _conn_str(conn_str)
    {
        auto f = std::bind(&CmdQueueRunner<T>::_run, this);
        thread = std::thread(f);
    }

    ~CmdQueueRunner() = default;

    void stop_running()
    {
        this->_keep_running = false;
    }
};

template class CmdQueueRunner<NixQueueCmd>;
template class CmdQueueRunner<SqlQueueCmd>;

#endif // CMDQUEUERUNNER_H
