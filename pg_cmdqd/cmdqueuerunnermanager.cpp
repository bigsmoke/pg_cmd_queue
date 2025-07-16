#include "cmdqueuerunnermanager.h"

#include <signal.h>
#include <unistd.h>

#include "pq-raii/libpq-raii.hpp"
#include "sigstate.h"

CmdQueueRunnerManager::CmdQueueRunnerManager(
        const std::string &conn_str,
        const bool emit_sigusr1_when_ready,
        const std::vector<std::string> &explicit_cmd_classes)
    : _conn_str(conn_str),
      _kill_pipe_fds(0),
      emit_sigusr1_when_ready(emit_sigusr1_when_ready),
      explicit_cmd_classes(explicit_cmd_classes)
{
    sigemptyset(&_sigset_masked_in_runner_threads);
    sigaddset(&_sigset_masked_in_runner_threads, SIGTERM);
    sigaddset(&_sigset_masked_in_runner_threads, SIGINT);
}

CmdQueueRunnerManager *CmdQueueRunnerManager::make_instance(
            const std::string &conn_str,
            const bool emit_sigusr1_when_ready,
            const std::vector<std::string> &explicit_cmd_classes)
{
    _instance = new CmdQueueRunnerManager(conn_str, emit_sigusr1_when_ready, explicit_cmd_classes);
    return _instance;
}

CmdQueueRunnerManager *CmdQueueRunnerManager::get_instance()
{
    return CmdQueueRunnerManager::_instance;
}

void CmdQueueRunnerManager::maintain_connection(bool one_shot)
{
    ::maintain_connection(_conn_str, _conn, one_shot);
}

std::vector<std::string> CmdQueueRunnerManager::get_cmd_class_names()
{
    maintain_connection(true);

    if (!_conn || PQ::status(_conn) != CONNECTION_OK)
        throw std::runtime_error("No connection.");

    std::vector<std::string> return_value;

    // TODO: Rowan's API pattern is that we get this info from schema cmdqd, but the view there is filtered.
    PG::result qresult = PQ::exec(_conn, "SELECT cmd_class FROM cmdq.cmd_queue");

    if (PQ::ntuples(qresult) == 0)
        throw std::runtime_error("No result");

    if (PQ::resultStatus(qresult) != PGRES_TUPLES_OK)
        throw std::runtime_error(PQ::resultErrorMessage(qresult));

    std::unordered_map<std::string, int> fieldNumbers = PQ::fnumbers(qresult);
    for (int i = 0; i < PQntuples(qresult.get()); i++)
    {
        std::string v = PQ::getvalue(qresult, i, fieldNumbers.at("cmd_class"));
        return_value.push_back(v);
    }

    return return_value;
}

bool CmdQueueRunnerManager::queue_has_runner_already(const CmdQueue &cmd_queue)
{
    return _nix_cmd_queue_runners.count(cmd_queue.cmd_class_identity) > 0
           or _sql_cmd_queue_runners.count(cmd_queue.cmd_class_identity) > 0;
}

// TODO: Check if changed queues are adequately restarted
void CmdQueueRunnerManager::refresh_queue_list(const bool retry_select)
{
    static const int max_retry_seconds = 60;

    PG::result result(nullptr);
    int retry_seconds = 1;
    while (_keep_running)
    {
        result = PQ::execParams(_conn, CmdQueue::SELECT_STMT, 1, {},
                                {PQ::as_text_array(explicit_cmd_classes)});

        if (PQ::resultStatus(result) == PGRES_TUPLES_OK) break;

        logger->log(LOG_ERROR, "Failed to SELECT cmd_queue list: %s",
                    PQ::resultErrorMessage(result).c_str());
        if (not retry_select) return;
        logger->log(LOG_INFO, "Will SELECT again in %i seconds…", retry_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(retry_seconds));
        if (sig_num_received({SIGTERM, SIGINT, SIGQUIT}) != 0)
        {
            logger->log(LOG_DEBUG4, "`std::this_thread::sleep_for()` interrupted by signal %i.",
                        sig_num_received({SIGTERM, SIGINT, SIGQUIT}));
            _keep_running = false;
        }
        if (retry_seconds * 2 <= max_retry_seconds) retry_seconds *= 2;
    }

    _new_cmd_classes.clear();
    std::unordered_map<std::string, int> fieldNumbers = PQ::fnumbers(result);
    for (int i = 0; i < PQntuples(result.get()); i++)
    {
        CmdQueue cmd_queue(result, i, fieldNumbers);

        if (!cmd_queue.is_valid())
        {
            logger->log(LOG_ERROR, cmd_queue.validation_error_message().c_str());
            continue;
        }

        if (not queue_has_runner_already(cmd_queue))
            this->add_runner(cmd_queue);
        _new_cmd_classes.insert(cmd_queue.cmd_class_identity);
    }

    for (const std::string &old_cmd_class : _old_cmd_classes)
    {
        if (_new_cmd_classes.count(old_cmd_class) == 0)
        {
            logger->log(LOG_DEBUG5, "Asking runner `%s` thread for disappeared `cmd_queue` to stop.",
                        old_cmd_class.c_str());
            this->stop_runner(old_cmd_class, SIGTERM);
        }
    }
    _old_cmd_classes = _new_cmd_classes;
}

void CmdQueueRunnerManager::listen_for_queue_list_changes()
{
    PG::result listen_result = PQ::exec(_conn, "LISTEN cmdq");  // TODO: Get from setting wrapper
    if (PQresultStatus(listen_result.get()) != PGRES_COMMAND_OK)
    {
        logger->log(LOG_ERROR, "Failed to `LISTEN` for `NOTIFY` events on the `cmdq` channel: %s",
                    PQerrorMessage(_conn->get()));
        return;
    }
    logger->log(LOG_DEBUG3, "Listening to cmdq channel for changes to the `cmd_queue` table.");

    // TODO: We should actual emit this signal when all the threads for the currently extant queues are is_prepared()
    if (emit_sigusr1_when_ready and not _emitted_sigusr1_yet)
    {
        kill(getppid(), SIGUSR1);  // Tell the parent process that we're ready _and_ listening.
        _emitted_sigusr1_yet = true;
    }

    struct pollfd fds[] = {
        { PQ::socket(_conn), POLLIN | POLLPRI, 0 },
        { _kill_pipe_fds.read_fd(), POLLIN | POLLPRI, 0 },
    };

    if (fds[0].fd < 0)
    {
        logger->log(LOG_ERROR, "Could not get socket of libpq connection: %s",
                    PQerrorMessage((_conn->get())));
        return;
    }

    while (_keep_running)
    {
        int fd_count = poll(fds, 2, -1);
        if (fd_count < 0)
        {
            if (errno == EINTR) continue;
            logger->log(LOG_ERROR, "poll() failed: %s", strerror(errno));
            return;
        }

        if (fds[0].revents != 0)
        {
            PQ::consumeInput(_conn);
            while (_keep_running)
            {
                std::shared_ptr<PG::notify> notify = PQ::notifies(_conn);
                if (notify->get() == nullptr)
                    break;
                logger->log(LOG_INFO, "Received a NOTIFY event on the `%s` channel: %s", notify->relname().c_str(), notify->extra().c_str());
                std::vector<std::optional<std::string>> payload_fields = PQ::from_text_composite_value((notify->extra()));
                // TODO: replicate parsing logic from cmdqueuerunner.h
                if (payload_fields.at(0) == "cmd_queue" and (payload_fields.at(2) == "INSERT"
                                                             or payload_fields.at(2) == "UPDATE"
                                                             or payload_fields.at(2) == "DELETE"))
                {
                    refresh_queue_list(true);
                }

                PQ::consumeInput(_conn);
            }
        }

        if (fds[1].revents != 0)
        {
            int sig_num = -1;
            // TODO: Replicate logic from CmdQueueRunner._run()
            ssize_t n = 0;
            ssize_t bytes_read = 0;
            while ((n = read(_kill_pipe_fds.read_fd(), &sig_num, sizeof(int) - bytes_read)) != 0)
            {
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                bytes_read += n;
            }
            logger->log(LOG_DEBUG1,
                        "Exiting `poll()` loop after receiving `kill(%i)` signal via pipe.",
                        sig_num);
            _keep_running = false;
        }
    }
}

void CmdQueueRunnerManager::add_runner(const CmdQueue &cmd_queue)
{
    // The signals that we don't want the runner threads to received are first blocked here in the main
    // thread, to avoid the race condition that a signal is received by the child thread before it had the
    // opportunity to call `pthread_signmask()`.  We don't have to worry about the signal _not_ arriving at
    // all while the runner thread is being spawned, because signals are queued until _some_ thread is ready
    // to receive them.
    sigprocmask(SIG_BLOCK, &_sigset_masked_in_runner_threads, nullptr);

    if (cmd_queue.cmd_signature_class_relname == "nix_queue_cmd_template")
    {
        _nix_cmd_queue_runners.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(cmd_queue.cmd_class_identity),
            std::forward_as_tuple(cmd_queue, _conn_str)
        );
    }
    else if (cmd_queue.cmd_signature_class_relname == "sql_queue_cmd_template")
    {
        _sql_cmd_queue_runners.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(cmd_queue.cmd_class_identity),
            std::forward_as_tuple(cmd_queue, _conn_str)
        );
    }
    else
    {
        logger->log(
            LOG_ERROR,
            "Command queue \x1b[1m%s\x1b[22m has unrecognized template type: \x1b[1m%s\x1b[22m",
            cmd_queue.cmd_class_identity.c_str(), cmd_queue.cmd_signature_class_relname.c_str()
        );
    }

    sigprocmask(SIG_UNBLOCK, &_sigset_masked_in_runner_threads, nullptr);
}

void CmdQueueRunnerManager::stop_runner(
        const std::string &cmd_class,
        const int simulate_signal)
{
    if (_nix_cmd_queue_runners.count(cmd_class) == 1)
        _nix_cmd_queue_runners.at(cmd_class).kill(simulate_signal);
    else if (_sql_cmd_queue_runners.count(cmd_class) == 1)
        _sql_cmd_queue_runners.at(cmd_class).kill(simulate_signal);
    else
        throw std::runtime_error("Could not find runner for queue_cmd_class = '" + cmd_class + "'");
}

void CmdQueueRunnerManager::stop_all_runners()
{
    int sig_num = sig_num_received({SIGQUIT, SIGTERM, SIGINT});

    if (sig_num > 0)
        logger->log(LOG_INFO, "Passing the `kill(%i)` signal on to all remaining runner threads.", sig_num);

    for (auto &pair: _nix_cmd_queue_runners)
        pair.second.kill(sig_num);
    for (auto &pair: _sql_cmd_queue_runners)
        pair.second.kill(sig_num);
}

void CmdQueueRunnerManager::join_all_threads()
{
    for (auto &pair : _nix_cmd_queue_runners)
        if (pair.second.thread.joinable())
            pair.second.thread.join();
    for (auto &pair : _sql_cmd_queue_runners)
        if (pair.second.thread.joinable())
            pair.second.thread.join();
}

void CmdQueueRunnerManager::receive_signal(const int sig_num)
{
    sig_recv[sig_num] = true;

    if (sig_num == SIGTERM or sig_num == SIGINT or sig_num == SIGQUIT)
    {
        // Write signal number to the pipe, to bust the `poll()` loop in the runner thread out of its wait.
        // We stupidly write the binary representation of the `int`, knowing that the endianness at the other
        // end of the pipe is the same, since we're the same program.
        const ssize_t len = sizeof(int);
        ssize_t i = 0;
        while (i < len)
        {
            const size_t n = write(_kill_pipe_fds.write_fd(), &sig_num + i, len - i);

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }

            i += n;
        }
    }
}

void signal_handler(const int sig_num)
{
    CmdQueueRunnerManager *manager = CmdQueueRunnerManager::get_instance();
    if (!manager) return;

    manager->receive_signal(sig_num);
}

void CmdQueueRunnerManager::install_signal_handlers()
{
    struct sigaction sig_handler;
    sig_handler.sa_handler = signal_handler;
    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_flags = 0;
    sigaction(SIGTERM, &sig_handler, nullptr);
    sigaction(SIGINT, &sig_handler, nullptr);
    sigaction(SIGQUIT, &sig_handler, nullptr);
}
