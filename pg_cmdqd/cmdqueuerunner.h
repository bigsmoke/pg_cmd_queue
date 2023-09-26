#ifndef CMDQUEUERUNNER_H
#define CMDQUEUERUNNER_H

#include <functional>
#include <thread>

#include "libpq-fe.h"
#include "poll.h"

#include "pq-raii/libpq-raii.hpp"
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

    void _maintain_connection(std::shared_ptr<PG::conn> &conn)
    {
        if (conn and PQ::status(conn) == CONNECTION_OK)
            return;

        static const int max_connect_retry_seconds = 60;

        if (not conn)
        {
            if (not _conn_str.empty())
                logger->log(LOG_INFO, "Connecting to database: \x1b[1m%s\x1b[22m", _conn_str.c_str());
            else
                logger->log(LOG_DEBUG1, "No connectiong string given; letting libpq figure out what to do from the \x1b[1mPG*\x1b[22m environment variables…");

            int connect_retry_seconds = 1;
            while (true)
            {
                conn = PQ::connectdb(_conn_str);
                if (PQ::status(conn) == CONNECTION_OK)
                {
                    logger->log(
                        LOG_INFO,
                        "DB connection established to \x1b[1m%s\x1b[22m on \x1b[1m%s:%s\x1b[22m as \x1b[1m%s\x1b[22m",
                        PQdb(conn->get()), PQhost(conn->get()), PQport(conn->get()), PQuser(conn->get())
                    );
                    break;
                }

                logger->log(LOG_ERROR, "Failed to connect to database: %s", PQerrorMessage(conn->get()));
                logger->log(LOG_INFO, "Will retry connecting in %i seconds…", connect_retry_seconds);
                std::this_thread::sleep_for(std::chrono::seconds(connect_retry_seconds));
                if (connect_retry_seconds * 2 <= max_connect_retry_seconds) connect_retry_seconds *= 2;
            }
        }
        else if (PQ::status(conn) == CONNECTION_BAD)
        {
            // TODO: It would probably be better to exit the thread and let the main thread restart it when needed
            int connect_retry_seconds = 1;
            while (true)
            {
                PQ::reset(conn);
                if (PQ::status(conn) == CONNECTION_OK)
                    break;

                logger->log(LOG_ERROR, "Failed to reset database connection: %s", PQerrorMessage(conn->get()));
                logger->log(LOG_INFO, "Will retry reset in %i seconds…", connect_retry_seconds);
                std::this_thread::sleep_for(std::chrono::seconds(connect_retry_seconds));
                if (connect_retry_seconds * 2 <= max_connect_retry_seconds) connect_retry_seconds *= 2;
            }
        }
    }

    void _setup_session(std::shared_ptr<PG::conn> & conn)
    {
        PQ::exec(conn, "SET search_path TO cmdq");

        if (_cmd_queue.queue_runner_role)
        {
            logger->log(LOG_INFO, "Setting role to \x1b[1m%s\x1b[22m", _cmd_queue.queue_runner_role.value().c_str());
            std::shared_ptr<PG::result> set_role_result = PQ::exec(conn, formatString(
                "SET ROLE TO %s",
                PQescapeIdentifier(
                    conn->get(),
                    _cmd_queue.queue_runner_role.value().c_str(),
                    _cmd_queue.queue_runner_role.value().length()
                )
            ));
            if (PQ::resultStatus(set_role_result) != PGRES_COMMAND_OK)
            {
                logger->log(LOG_ERROR, "`SET ROLE` command failed: %s", PQ::resultErrorMessage(set_role_result).c_str());
                return;
            }
        }

        if (_cmd_queue.queue_notify_channel)
        {
            // Listen:
            PQ::exec(conn, std::string("LISTEN \"") + _cmd_queue.queue_notify_channel.value() + "\"");
            // And lets the world know that we're listening:
            PQ::execParams(
                conn,
                "SELECT pg_notify(pg_cmd_queue_notify_channel(), row($1, $2, $3)::text)",
                3, {}, {CmdQueue::CMD_QUEUE_RELNAME, _cmd_queue.queue_cmd_relname, "LISTEN"}
            );
        }
    }

    void _prepare_statements(std::shared_ptr<PG::conn> &conn)
    {
        PQ::prepare(conn, "select_oldest", T::select_oldest(_cmd_queue));
        //pg.prepare("select_notify", T::select_notify(_cmd_queue));
        PQ::prepare(conn, "select_random", T::select_random(_cmd_queue));
    }

    void _notify_preparedness(std::shared_ptr<PG::conn> &conn)
    {
        // Let the listener(s) know that we're about to start selecting things from the queue
        std::shared_ptr<PG::result> result = PQ::execParams(
            conn,
            "SELECT pg_notify(pg_cmd_queue_notify_channel(), row($1::text, $2::text, $3::text)::text)",
            3, {}, {CmdQueue::CMD_QUEUE_RELNAME, _cmd_queue.queue_cmd_relname, "PREPARE"}
        );

        if (PQ::resultStatus(result) != PGRES_TUPLES_OK)
        {
            logger->log(LOG_ERROR, "Failed to `NOTIFY` preparedness: %s", PQ::resultErrorMessage(result).c_str());
        }
    }

    void _run()
    {
        Logger::cmd_queue = std::make_shared<CmdQueue>(_cmd_queue);

        std::shared_ptr<PG::conn> conn = nullptr;
        struct pollfd poll_fds[1];

        std::unordered_map<std::string, int> selected_field_numbers;

        while (this->_keep_running)
        {
            _maintain_connection(conn);
            poll_fds[0] = { PQ::socket(conn), POLLIN | POLLPRI, 0 };

            _setup_session(conn);

            _prepare_statements(conn);
            if (selected_field_numbers.size() == 0)
            {
                // The field mappings are the same for all the `SELECT` statements of the same `T`.
                selected_field_numbers = [conn]()
                {
                    std::shared_ptr<PG::result> result = PQ::describePrepared(conn, "select_oldest");
                    return PQ::fnumbers(result);
                }();
            }
            _notify_preparedness(conn);

            int reselect_found_count = 0;
            while (this->_keep_running)
            {
                logger->log(LOG_DEBUG3, "Checking for item in queue…");
                PQ::exec(conn, "BEGIN TRANSACTION");

                std::shared_ptr<PG::result> select_result;
                if (_cmd_queue.queue_reselect_randomized_every_nth
                    and reselect_found_count % _cmd_queue.queue_reselect_randomized_every_nth.value() == 0)
                {
                    select_result = PQ::execPrepared(conn, "select_random");
                }
                else
                {
                    select_result = PQ::execPrepared(conn, "select_oldest");
                }

                if (PQ::resultStatus(select_result) != PGRES_TUPLES_OK)
                {
                    logger->log(LOG_ERROR, "Retrieving command from queue failed: %s",
                            PQerrorMessage(conn->get()));
                }
                else if (PQntuples(select_result->get()) == 1)
                {
                    reselect_found_count++;

                    T queue_cmd(select_result, 0, selected_field_numbers);

                    // Delegate the execution of the command to the specific `(Nix|Sql|Http)QueueCommand`.
                    // `conn` is passed to `run_cmd()` solely because `SqlQueueCommand` needs the connection.
                    queue_cmd.run_cmd(conn);

                    std::shared_ptr<PG::result> update_result = PQ::execParams(
                                conn,
                                queue_cmd.update_stmt(_cmd_queue),
                                queue_cmd.update_params().size(),
                                {},
                                queue_cmd.update_params());
                    if (PQ::resultStatus(update_result) != PGRES_COMMAND_OK)
                    {
                        logger->log(LOG_ERROR, "SQL UPDATE for command %s failed: %s",
                                    queue_cmd.meta.cmd_id.c_str(), PQ::resultErrorMessage(update_result).c_str());
                    }
                }

                if (PQ::transactionStatus(conn) == PQTRANS_UNKNOWN)
                    break;  // Go back to main (re)connect looop
                if (PQ::transactionStatus(conn) == PQTRANS_INERROR)
                    PQ::exec(conn, "ROLLBACK TRANSACTION");
                else
                    PQ::exec(conn, "COMMIT TRANSACTION");

                int fd_count = poll(poll_fds, 1, _cmd_queue.queue_reselect_interval_msec);
                if (fd_count < 0)
                {
                    if (errno == EINTR) continue;
                    logger->log(LOG_ERROR, "poll() failed dramatically: %s", strerror(errno));
                    return;  // Leave this runner thread.
                }
                if (not PQ::consumeInput(conn))
                {
                    logger->log(LOG_ERROR, "PQconsumeInput() errored: %s", PQ::errorMessage(conn).c_str());
                    break;  // Go back to the main (re)connect loop
                }
            } // (re)select loop
        } // (re)connect loop
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
