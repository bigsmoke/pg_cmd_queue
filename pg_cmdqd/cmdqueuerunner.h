#ifndef CMDQUEUERUNNER_H
#define CMDQUEUERUNNER_H

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
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

extern char **environ;

template <typename T>
class CmdQueueRunner
{
    bool _running = false;
    bool _keep_running = true;
    CmdQueue _cmd_queue;
    std::string _conn_str;
    Logger *logger = Logger::getInstance();
    bool _is_prepared = false;

    void _run()
    {
        _running = true;

        Logger::cmd_queue = std::make_shared<CmdQueue>(_cmd_queue); // FIXME: This makes a copy

        const std::unordered_map<std::string, std::string> cmdqd_env = environ_to_unordered_map(environ);

        std::shared_ptr<PG::conn> conn = nullptr;
        struct pollfd poll_fds[2];

        std::unordered_map<std::string, int> selected_field_numbers;

        while (this->_keep_running)
        {
            maintain_connection(_conn_str, conn);

            {
                // The `cmdqd.runner_session_start()` function:
                //   1. `SET`s SQL-level settings for the queue, and
                //   2. `PREPARE`s the statements we will use to `SELECT FROM` and `UPDATE` the cmd queue.
                PG::result proc_result = PQ::execParams(
                        conn, std::string("CALL cmdqd.runner_session_start($1::regclass, $2)"),
                        2, {}, {_cmd_queue.cmd_class_identity, PQ::as_text_hstore(cmdqd_env)});
                if (PQ::resultStatus(proc_result) != PGRES_COMMAND_OK)
                {
                    logger->log(LOG_ERROR, "Failure during `runner_session_start()`: %s",
                                PQ::resultErrorMessage(proc_result).c_str());
                    break;  // We have no way to recover from this (yet) without getting into an infinite loop.
                }
            }

            poll_fds[0] = {PQ::socket(conn), POLLIN | POLLPRI, 0};
            poll_fds[1] = {kill_pipe_fds.read_fd(), POLLIN | POLLPRI, 0};

            if (selected_field_numbers.size() == 0)
            {
                // The field mappings are the same for all the `SELECT` statements of the same `T`.
                PG::result result = PQ::describePrepared(conn, "select_oldest_cmd");
                selected_field_numbers = PQ::fnumbers(result);
            }

            int reselect_round = 0;
            std::chrono::steady_clock::time_point reselect_next_when = std::chrono::steady_clock::now();

            std::optional<std::tuple<std::string, std::string, std::optional<std::string>>> notify_cmd = {};
            bool go_back_to_reconnect_loop = false;
            while (this->_keep_running)
            {
                if (go_back_to_reconnect_loop)
                    break;

                PQ::exec(conn, "BEGIN TRANSACTION");

                PG::result select_result(nullptr);

                if (notify_cmd)
                {
                    logger->log(LOG_DEBUG3,
                                "Getting cmd `WHERE cmd_id = '%s' AND cmd_subid IS NOT DISTINCT FROM %s` from queue…",
                                std::get<1>(notify_cmd.value()).c_str(),
                                std::get<2>(notify_cmd.value()).has_value()
                                    ? (std::string("'") + std::get<2>(notify_cmd.value()).value() + "'").c_str()
                                    : "NULL");

                    select_result = PQ::execPrepared(
                        conn, "select_notify_cmd", 2,
                        {std::get<1>(notify_cmd.value()), std::get<2>(notify_cmd.value())});
                }
                else if (_cmd_queue.queue_reselect_randomized_every_nth and reselect_round % _cmd_queue.queue_reselect_randomized_every_nth.value() == 0)
                {
                    logger->log(LOG_DEBUG3, "Getting random queue_cmd from cmd_queue…");
                    select_result = PQ::execPrepared(conn, "select_random_cmd");
                }
                else
                {
                    logger->log(LOG_DEBUG3, "Getting oldest queue_cmd from cmd_queue…");
                    select_result = PQ::execPrepared(conn, "select_oldest_cmd");
                }

                if (PQ::resultStatus(select_result) != PGRES_TUPLES_OK)
                {
                    logger->log(LOG_ERROR, "Retrieving command from queue failed: %s",
                                PQerrorMessage(conn->get()));
                }
                else if (PQntuples(select_result.get()) == 1)
                {
                    T queue_cmd(select_result, 0, selected_field_numbers);

                    queue_cmd.meta.stamp_start_time();

                    logger->log(LOG_NOTICE, "Starting cmd_id = %s (%s)", queue_cmd.meta.cmd_id.c_str(), queue_cmd.meta.cmd_class_identity.c_str());

                    // Delegate the execution of the command to the specific `(Nix|Sql|Http)QueueCommand`.
                    // `conn` is passed to `run_cmd()` solely because `SqlQueueCommand` needs the connection.
                    queue_cmd.run_cmd(conn, _cmd_queue.queue_cmd_timeout_sec);

                    logger->log(LOG_NOTICE, "Finished cmd_id = %s (%s)", queue_cmd.meta.cmd_id.c_str(), queue_cmd.meta.cmd_class_identity.c_str());

                    queue_cmd.meta.stamp_end_time();

                    PG::result update_result = PQ::execPrepared(
                            conn, "update_cmd", queue_cmd.update_params().size(),
                            queue_cmd.update_params(), queue_cmd.update_param_lengths(),
                            queue_cmd.update_param_formats());
                    if (PQ::resultStatus(update_result) != PGRES_COMMAND_OK)
                    {
                        logger->log(LOG_ERROR, "SQL UPDATE for command %s failed: %s",
                                    queue_cmd.meta.cmd_id.c_str(), PQ::resultErrorMessage(update_result).c_str());

                        PQ::exec(conn, "ROLLBACK TRANSACTION");

                        PG::result log_failed_update_result = PQ::execParams(
                                conn, "CALL cmdqd.remember_failed_update_for_this_reselect_round($1, $2)",
                                2, {}, {queue_cmd.meta.cmd_id, queue_cmd.meta.cmd_subid});
                        if (PQ::resultStatus(log_failed_update_result) != PGRES_COMMAND_OK)
                        {
                            logger->log(LOG_ERROR, "Even registering the failed update failed: %s",
                                        PQ::resultErrorMessage(log_failed_update_result).c_str());
                        }
                    }
                }
                else
                {
                    assert(PQ::ntuples(select_result) == 0);

                    if (not notify_cmd)
                    {
                        // There might be a race condition, where the regular (re)`SELECT` loop has already picked
                        // up a queue_cmd, thereby causing the `cmd_id`/`cmd_subid`-qualified `SELECT` for that
                        // specific cmd to yield no rows.  _But_, we might have still had other commands in the
                        // queue before we spent an iteration on `SELECT`ing the command from the `NOTIFY`.
                        // That's why we only postpone the re`SELECT` when we were _not_ `SELECT`ing in response
                        // to a `NOTIFY` event.
                        reselect_next_when = std::chrono::steady_clock::now() + std::chrono::milliseconds(_cmd_queue.queue_reselect_interval_msec);

                        PG::result proc_result(PQ::exec(
                                conn, std::string("SELECT reselect_round FROM cmdqd.enter_reselect_round()")));
                        if (PQ::resultStatus(proc_result) != PGRES_TUPLES_OK)
                        {
                            logger->log(LOG_ERROR, "Failure during `enter_reselect_round()`: %s",
                                        PQ::resultErrorMessage(proc_result).c_str());
                            return;  // FIXME: We should find a way to properly recover.
                        }

                        const std::string result_round_str = PQ::getvalue(proc_result, 0, 0);
                        reselect_round = std::stoi(result_round_str);

                        logger->log(LOG_DEBUG5,
                                    "Before the next (re)select round, we're going to poll() and wait for ~ %i msec",
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                        reselect_next_when - std::chrono::steady_clock::now())
                                        .count());
                    }
                    else
                    {
                        logger->log(LOG_DEBUG5, "Could not find new cmd that I was notified of in queue");
                    }
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
               or (kill_pipe_bytes_written < 0 and errno == EINTR))
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
