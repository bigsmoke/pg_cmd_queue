#ifndef CMDQUEUERUNNER_H
#define CMDQUEUERUNNER_H

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <libpq-fe.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include "pq-raii/libpq-raii.hpp"
#include "pq_cmdqd_utils.h"
#include "cmdqueue.h"
#include "logger.h"
#include "nixqueuecmd.h"
#include "pipefds.h"
#include "sqlqueuecmd.h"
#include "utils.h"

template <typename T>
class CmdQueueRunner
{
    bool _running = false;
    bool _keep_running = true;
    CmdQueue _cmd_queue;
    std::string _conn_str;
    Logger *logger = Logger::getInstance();
    bool _is_prepared = false;

    void _setup_session(std::shared_ptr<PG::conn> &conn)
    {
        PQ::exec(conn, "SET search_path TO cmdq");

        /*
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
            }
        }
        */

        if (_cmd_queue.queue_notify_channel)
        {
            // Listen:
            std::shared_ptr<PG::result> listen_result = PQ::exec(
                conn, std::string("LISTEN \"") + _cmd_queue.queue_notify_channel.value() + "\"");
            if (PQ::resultStatus(listen_result) != PGRES_COMMAND_OK)
                logger->log(LOG_ERROR, "Failed to LISTEN on `%s` channel: %s",
                            _cmd_queue.queue_notify_channel.value().c_str(),
                            PQ::resultErrorMessage(listen_result).c_str());

            // And lets the world know that we're listening:
            std::shared_ptr<PG::result> notify_listen_result = PQ::execParams(
                conn,
                "SELECT pg_notify(pg_cmd_queue_notify_channel(), row($1::text, $2::text, $3::text)::text)",
                3, {}, {CmdQueue::CMD_QUEUE_RELNAME, _cmd_queue.cmd_class_identity, "LISTEN"});
            if (PQ::resultStatus(notify_listen_result) != PGRES_TUPLES_OK)
                logger->log(LOG_ERROR, "Failed to `NOTIFY` listening status: %s",
                            PQ::resultErrorMessage(notify_listen_result).c_str());
        }
    }

    void _prepare_statements(std::shared_ptr<PG::conn> &conn)
    {
        {
            std::shared_ptr<PG::result> result = PQ::prepare(
                conn, "select_oldest", T::select_stmt(_cmd_queue, {}, "cmd_queued_since"));
            if (PQ::resultStatus(result) != PGRES_COMMAND_OK)
            {
                logger->log(LOG_ERROR, "Preparing `select_oldest` statement failed: %s",
                            PQerrorMessage(conn->get()));
            }
        }
        {
            // If `ORDER BY random() LIMIT` turns out to be too slow we could do:
            // `OFFSET floor(random() * (SELECT count(*) FROM <some_nix_queue_cmd>)) LIMIT 1`
            std::shared_ptr<PG::result> result = PQ::prepare(
                conn, "select_random", T::select_stmt(_cmd_queue, {}, "random()"));
            if (PQ::resultStatus(result) != PGRES_COMMAND_OK)
            {
                logger->log(LOG_ERROR, "Preparing `select_random` statement failed: %s",
                            PQerrorMessage(conn->get()));
            }
        }
        {
            std::shared_ptr<PG::result> result = PQ::prepare(
                conn, "select_notified",
                T::select_stmt(_cmd_queue, "cmd_id = $1 AND cmd_subid IS NOT DISTINCT FROM $2", {}));
            if (PQ::resultStatus(result) != PGRES_COMMAND_OK)
            {
                logger->log(LOG_ERROR, "Preparing `select_notified` statement failed: %s",
                            PQerrorMessage(conn->get()));
            }
        }
    }

    void _notify_preparedness(std::shared_ptr<PG::conn> &conn)
    {
        // Let the listener(s) know that we're about to start selecting things from the queue
        std::shared_ptr<PG::result> result = PQ::execParams(
            conn,
            "SELECT pg_notify(pg_cmd_queue_notify_channel(), row($1::text, $2::text, $3::text)::text)",
            3, {}, {CmdQueue::CMD_QUEUE_RELNAME, _cmd_queue.cmd_class_identity, "PREPARE"});

        if (PQ::resultStatus(result) != PGRES_TUPLES_OK)
        {
            logger->log(LOG_ERROR, "Failed to `NOTIFY` preparedness: %s", PQ::resultErrorMessage(result).c_str());
            _is_prepared = false;
        }
        else
            _is_prepared = true;
    }

    void _run()
    {
        _running = true;

        Logger::cmd_queue = std::make_shared<CmdQueue>(_cmd_queue); // FIXME: This makes a copy

        std::shared_ptr<PG::conn> conn = nullptr;
        struct pollfd poll_fds[2];

        std::unordered_map<std::string, int> selected_field_numbers;

        while (this->_keep_running)
        {
            maintain_connection(_conn_str, conn);
            poll_fds[0] = {PQ::socket(conn), POLLIN | POLLPRI, 0};
            poll_fds[1] = {kill_pipe_fds.read_fd(), POLLIN | POLLPRI, 0};

            _setup_session(conn);

            _prepare_statements(conn);
            if (selected_field_numbers.size() == 0)
            {
                // The field mappings are the same for all the `SELECT` statements of the same `T`.
                std::shared_ptr<PG::result> result = PQ::describePrepared(conn, "select_oldest");
                selected_field_numbers = PQ::fnumbers(result);
            }
            _notify_preparedness(conn);

            int reselect_found_count = 0;
            std::chrono::steady_clock::time_point reselect_next_when = std::chrono::steady_clock::now();

            std::optional<std::tuple<std::string, std::string, std::optional<std::string>>> notify_cmd = {};
            bool go_back_to_reconnect_loop = false;
            while (this->_keep_running)
            {
                if (go_back_to_reconnect_loop)
                    break;

                PQ::exec(conn, "BEGIN TRANSACTION");

                std::shared_ptr<PG::result> select_result;

                if (notify_cmd)
                {
                    logger->log(LOG_DEBUG3,
                                "Getting cmd `WHERE cmd_id = '%s' AND cmd_subid IS NOT DISTINCT FROM %s` from queue…",
                                std::get<1>(notify_cmd.value()).c_str(),
                                std::get<2>(notify_cmd.value()).has_value()
                                    ? (std::string("'") + std::get<2>(notify_cmd.value()).value() + "'").c_str()
                                    : "NULL");

                    select_result = PQ::execPrepared(
                        conn, "select_notified", 2,
                        {std::get<1>(notify_cmd.value()), std::get<2>(notify_cmd.value())});
                }
                else if (_cmd_queue.queue_reselect_randomized_every_nth and reselect_found_count % _cmd_queue.queue_reselect_randomized_every_nth.value() == 0)
                {
                    logger->log(LOG_DEBUG3, "Getting random queue_cmd from cmd_queue…");
                    select_result = PQ::execPrepared(conn, "select_random");
                }
                else
                {
                    logger->log(LOG_DEBUG3, "Getting oldest queue_cmd from cmd_queue…");
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

                    queue_cmd.meta.stamp_start_time();

                    // Delegate the execution of the command to the specific `(Nix|Sql|Http)QueueCommand`.
                    // `conn` is passed to `run_cmd()` solely because `SqlQueueCommand` needs the connection.
                    queue_cmd.run_cmd(conn, _cmd_queue.queue_cmd_timeout_sec);

                    queue_cmd.meta.stamp_end_time();

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
                else
                {
                    assert(PQntuples(select_result->get()) == 0);

                    if (not notify_cmd)
                    {
                        // There might be a race condition, where the regular (re)`SELECT` loop has already picked
                        // up a queue_cmd, thereby causing the `cmd_id`/`cmd_subid`-qualified `SELECT` for that
                        // specific cmd to yield no rows.  _But_, we might have still had other commands in the
                        // queue before we spent an iteration on `SELECT`ing the command from the `NOTIFY`.
                        // That's why we only postpone the re`SELECT` when we were _not_ `SELECT`ing in response
                        // to a `NOTIFY` event.
                        reselect_next_when = std::chrono::steady_clock::now() + std::chrono::milliseconds(_cmd_queue.queue_reselect_interval_msec);

                        logger->log(LOG_DEBUG5,
                                    "After this transaction, we're going to poll() and wait for ~ %i msec",
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                        reselect_next_when - std::chrono::steady_clock::now())
                                        .count());
                    }
                    else
                        logger->log(LOG_DEBUG5, "Could not find new cmd that I was notified of in queue");
                }

                if (PQ::transactionStatus(conn) == PQTRANS_UNKNOWN)
                    break; // Go back to main (re)connect looop
                if (PQ::transactionStatus(conn) == PQTRANS_INERROR)
                    PQ::exec(conn, "ROLLBACK TRANSACTION");
                else
                    PQ::exec(conn, "COMMIT TRANSACTION");

                while (_keep_running)
                {
                    // We _start_ by checking for any notifications that might have entered the libpq queue
                    // while waiting for the results of any other SQL command earlier in the loop.
                    notify_cmd = {}; // Forget previous NOTIFY if one is still lingering in our memory.
                    std::shared_ptr<PG::notify> notify = PQ::notifies(conn);
                    if (notify->get() != nullptr)
                    {
                        logger->log(
                            LOG_DEBUG5,
                            "Received a NOTIFY event on the `%s` channel: %s",
                            notify->relname().c_str(),
                            notify->extra().c_str());

                        std::vector<std::optional<std::string>> notify_payload_fields;
                        try
                        {
                            // The payload of all the `pg_cmd_queue`-compatible notifcation should consist of a
                            // text-encoded composite value with:
                            // 1. the qualified name of the queue command relation;
                            // 2. the `cmd_id`; and
                            // 3. the optional `cmd_subid`.
                            notify_payload_fields = PQ::from_text_composite_value((notify->extra()));
                            if (notify_payload_fields.size() != 3)
                                throw std::runtime_error(formatString(
                                    "Expected 3 fields in composite NOTIFY payload, not %i",
                                    notify_payload_fields.size()));
                            if (not notify_payload_fields[0].has_value())
                                throw std::runtime_error(
                                    "The 1st field (`cmd_class_identity`) in NOTIFY payload may not be `NULL`");
                            if (not notify_payload_fields[1].has_value())
                                throw std::runtime_error(
                                    "The 2nd field (`cmd_id`) in NOTIFY payload may not be `NULL`");
                        }
                        catch (const std::exception &err)
                        {
                            logger->log(
                                LOG_ERROR,
                                "Could not parse the composite value expected in the NOTIFY payload: %s",
                                err.what());
                            continue; // Let's go check for another `NOTIFY` in the libpq queue.
                        }

                        if (notify_payload_fields[0].value() == _cmd_queue.cmd_class_identity)
                        {
                            logger->log(LOG_DEBUG1,
                                        "It appears as if this NOTIFY event on the `%s` channel is for me: %s",
                                        notify->relname().c_str(),
                                        notify->extra().c_str());
                            // Tell the next loop iteration that we have been notified of a new command in the
                            // queue and its `cmd_id` + (optional) `cmd_subid`.
                            notify_cmd = std::make_tuple(
                                notify_payload_fields[0].value(),
                                notify_payload_fields[1].value(),
                                notify_payload_fields[2]);
                            assert(std::get<2>(notify_cmd.value()).has_value() == false);
                            break; // We found a notication.  Let's go to the select loop to get the cmd.
                        }
                        else
                        {
                            // This notification was not for us.  Let's try if there is another one waiting.
                            continue;
                        }
                    }

                    std::chrono::milliseconds reselect_wait_time_left = std::chrono::duration_cast<
                        std::chrono::milliseconds>(reselect_next_when - std::chrono::steady_clock::now());

                    if (reselect_wait_time_left.count() < 0)
                        reselect_wait_time_left = std::chrono::milliseconds::zero();

                    int fd_count = poll(poll_fds, 2, reselect_wait_time_left.count());
                    if (fd_count < 0)
                    {
                        if (errno == EINTR)
                            continue; // We will see if `_keep_running` turned `false`.
                        logger->log(LOG_ERROR, "poll() failed: %s", strerror(errno));
                        _running = false;
                        return; // Leave this runner thread.
                    }
                    if (fd_count == 0)
                        break; // Time to go back to (re)select loop
                               //
                    if (poll_fds[0].revents != 0)
                    {
                        if (not PQ::consumeInput(conn))
                        {
                            logger->log(LOG_ERROR, "PQconsumeInput() failed: %s", PQ::errorMessage(conn).c_str());
                            go_back_to_reconnect_loop = true;
                            break; // Go back to the main (re)connect loop via the (re)select loop
                        }
                        // Input consumed; `PQnotifies()` should now be able to get any pending notifications
                        // in the next iteration of this loop.
                    }

                    if (poll_fds[1].revents != 0)  // poll_fd[1].fd = kill_pipe_fds.read_fd()
                    {
                        int sig_num = -1;
                        int sig_num_bytes_total = 0;
                        int sig_num_bytes_read = 0;
                        while ((sig_num_bytes_read = read(kill_pipe_fds.read_fd(),
                                                          &sig_num + sig_num_bytes_total * sizeof(char),
                                                          sizeof(int))) > 0)
                        {
                            sig_num_bytes_total += sig_num_bytes_read;
                        }
                        if (sig_num_bytes_read < 0 and sig_num_bytes_total < (int)sizeof(int))
                        {
                            if (errno == EINTR) continue;  // Another signal might have come in
                            if (errno == EAGAIN) continue;  // Maybe we have to wait for the rest of the `write()`?
                            logger->log(LOG_ERROR,
                                        "Unexpected error while reading from kill pipe FD: %s",
                                        strerror(errno));
                            _running = false;
                            return;
                        }
                        logger->log(LOG_DEBUG1,
                                    "Exiting `poll()` loop after receiving `kill(%i)` signal via pipe.",
                                    sig_num);
                        _keep_running = false;
                    }
                }  // PQnotify() & poll() loop

            }  // (re)select loop
        }  // (re)connect loop
        logger->log(LOG_DEBUG5, "Exited outer/(re)connect loop");

        _running = false;
    }

public:
    std::thread thread;

    PipeFds kill_pipe_fds;

    CmdQueueRunner() = delete;

    CmdQueueRunner(const CmdQueue &cmd_queue, const std::string &conn_str) : _cmd_queue(cmd_queue),
                                                                             _conn_str(conn_str),
                                                                             kill_pipe_fds(O_NONBLOCK)
    {
        auto f = std::bind(&CmdQueueRunner<T>::_run, this);
        thread = std::thread(f);

#ifdef _GNU_SOURCE
        pthread_t native = thread.native_handle();
        pthread_setname_np(native, cmd_queue.cmd_class_relname.substr(0, 15).c_str());
#endif
    }

    ~CmdQueueRunner() = default;

    bool running() const
    {
        return _running;
    }

    bool is_prepared() const
    {
        return _is_prepared;
    }

    void kill(int sig_num = 0)
    {
        if (not _running) return;

        if (sig_num > 0)
            logger->log(LOG_DEBUG5,
                        "Simulating `kill(%i)` signal to runner `%s` thread",
                        sig_num,
                        _cmd_queue.cmd_class_identity.c_str());

        // Write signal number to the pipe, to bust the `poll()` loop in the runner thread out of its wait.
        // We stupidly write the binary representation of the `int`, knowing that the endianness at the other
        // end of the pipe is the same, since we're the same program (though not the same thread).
        size_t kill_pipe_bytes_written = 0;
        size_t kill_pipe_bytes_to_write = sizeof(int);
        size_t kill_pipe_ptr_offset = 0;
        while ((kill_pipe_bytes_written = write(kill_pipe_fds.write_fd(),
                                                &sig_num + kill_pipe_ptr_offset,
                                                kill_pipe_bytes_to_write)
               ) > 0
               or (kill_pipe_bytes_written < 0 and errno == SIGINT))
        {
            kill_pipe_bytes_to_write -= kill_pipe_bytes_written;
            kill_pipe_ptr_offset += kill_pipe_bytes_written;
        }
        if (kill_pipe_bytes_written < 0)
        {
            // We an do this non-signal safe thing, because we're not in a signal handler.
            logger->log(LOG_ERROR,
                        "Error while trying to pass signal from main thread to event loop in runner thread: %s",
                        strerror(errno));
        }
    }
};

template class CmdQueueRunner<NixQueueCmd>;
template class CmdQueueRunner<SqlQueueCmd>;

#endif // CMDQUEUERUNNER_H
