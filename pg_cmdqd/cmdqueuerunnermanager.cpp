#include "cmdqueuerunnermanager.h"

#include <signal.h>
#include <unistd.h>

#include "pq-raii/libpq-raii.hpp"

CmdQueueRunnerManager::CmdQueueRunnerManager(const std::string &conn_str, const std::vector<std::string> &explicit_queue_cmd_classes)
    : _conn_str(conn_str),
      explicit_queue_cmd_classes(explicit_queue_cmd_classes)
{
    maintain_connection(conn_str, _conn);
    refresh_queue_list();
}

bool CmdQueueRunnerManager::queue_has_runner_already(const CmdQueue &cmd_queue)
{
    return _nix_cmd_queue_runners.count(cmd_queue.queue_cmd_relname) > 0
           or _sql_cmd_queue_runners.count(cmd_queue.queue_cmd_relname) > 0;
}

void CmdQueueRunnerManager::refresh_queue_list()
{
    static const int max_retry_seconds = 60;

    std::shared_ptr<PG::result> result;
    int retry_seconds = 1;
    while (true)
    {
        result = PQ::execParams(_conn, CmdQueue::SELECT_STMT, 1, {}, {PQ::as_text_array(explicit_queue_cmd_classes)});

        if (PQresultStatus(result->get())) break;

        logger->log(LOG_ERROR, "Failed to SELECT cmd_queue list: %s", PQ::resultErrorMessage(result).c_str());
        logger->log(LOG_INFO, "Will SELECT again in %i seconds…", retry_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(retry_seconds));
        if (retry_seconds * 2 <= max_retry_seconds) retry_seconds *= 2;
    }

    _new_queue_cmd_classes.clear();
    std::unordered_map<std::string, int> fieldNumbers = PQ::fnumbers(result);
    for (int i = 0; i < PQntuples(result->get()); i++)
    {
        CmdQueue cmd_queue(result, i, fieldNumbers);

        if (!cmd_queue.is_valid())
        {
            logger->log(LOG_ERROR, cmd_queue.validation_error_message().c_str());
            continue;
        }

        if (not queue_has_runner_already(cmd_queue))
            this->add_runner(cmd_queue);
        _new_queue_cmd_classes.insert(cmd_queue.queue_cmd_relname);
    }

    for (const std::string &old_cmd_class : _old_queue_cmd_classes)
        if (_new_queue_cmd_classes.count(old_cmd_class) == 0)
            this->stop_runner(old_cmd_class);
    _old_queue_cmd_classes = _new_queue_cmd_classes;
}

void CmdQueueRunnerManager::listen_for_queue_list_changes()
{
    std::shared_ptr<PG::result> listen_result = PQ::exec(_conn, "LISTEN cmdq");  // TODO: Get from setting wrapper
    if (PQresultStatus(listen_result->get()) != PGRES_COMMAND_OK)
    {
        logger->log(LOG_ERROR, "Failed to `LISTEN` for `NOTIFY` events on the `cmdq` channel: %s", PQerrorMessage(_conn->get()));
        return;
    }
    logger->log(LOG_DEBUG3, "Listening to cmdq channel for changes to the `cmd_queue` table.");

    // TODO: We should also emit a signal when all the threads for the currently extant queues are is_prepared()
    kill(getppid(), SIGUSR1);  // Tell the parent process that we're ready _and_ listening.

    struct pollfd fds[] = {
        { PQ::socket(_conn), POLLIN | POLLPRI, 0 }
    };

    if (fds[0].fd < 0)
    {
        logger->log(LOG_ERROR, "Could not get socket of libpq connection: %s", PQerrorMessage((_conn->get())));
        return;
    }

    while (true)
    {
        int fd_count = poll(fds, 1, -1);
        if (fd_count < 0)
        {
            if (errno == EINTR) continue;
            logger->log(LOG_ERROR, "poll() failed: %s", strerror(errno));
            return;
        }

        PQ::consumeInput(_conn);
        while (true)
        {
            std::shared_ptr<PG::notify> notify = PQ::notifies(_conn);
            if (notify->get() == nullptr)
                break;
            logger->log(LOG_INFO, "Received a NOTIFY event on the `%s` channel: %s", notify->relname().c_str(), notify->extra().c_str());
            std::vector<std::optional<std::string>> payload_fields = PQ::from_text_composite_value((notify->extra()));
            if (payload_fields[0] == "cmd_queue" and (payload_fields[2] == "INSERT" or payload_fields[2] == "UPDATE" or payload_fields[2] == "DELETE"))
            {
                refresh_queue_list();
            }

            PQconsumeInput(_conn->get());
        }
    }
}

void CmdQueueRunnerManager::add_runner(const CmdQueue &cmd_queue)
{
    if (cmd_queue.queue_signature_class == "nix_queue_cmd_template")
    {
        _nix_cmd_queue_runners.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(cmd_queue.queue_cmd_relname),
            std::forward_as_tuple(cmd_queue, _conn_str)
        );
    }
    else if (cmd_queue.queue_signature_class == "sql_queue_cmd_template")
    {
        _sql_cmd_queue_runners.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(cmd_queue.queue_cmd_relname),
            std::forward_as_tuple(cmd_queue, _conn_str)
        );
    }
    else
    {
        logger->log(
            LOG_ERROR,
            "Command queue \x1b[1m%s\x1b[22m has unrecognized template type: \x1b[1m%s\x1b[22m",
            cmd_queue.queue_cmd_relname.c_str(), cmd_queue.queue_signature_class.c_str()
        );
    }
}

void CmdQueueRunnerManager::stop_runner(const std::string &queue_cmd_class)
{
    if (_nix_cmd_queue_runners.count(queue_cmd_class) == 1)
        _nix_cmd_queue_runners.at(queue_cmd_class).stop_running();
    else if (_sql_cmd_queue_runners.count(queue_cmd_class) == 1)
        _sql_cmd_queue_runners.at(queue_cmd_class).stop_running();
    else
        throw std::runtime_error("Could not find runner for queue_cmd_class = '" + queue_cmd_class + "'");
}

void CmdQueueRunnerManager::join_all_threads()
{
    for (auto &it : _nix_cmd_queue_runners)
        if (it.second.thread.joinable())
            it.second.thread.join();
    for (auto &it : _sql_cmd_queue_runners)
        if (it.second.thread.joinable())
            it.second.thread.join();
}
