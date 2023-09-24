#include "cmdqueuerunnercollection.h"

#include "libpq-fe.h"
#include "lwpg_array.h"
#include "lwpg_notify.h"

CmdQueueRunnerCollection::CmdQueueRunnerCollection(const std::string &conn_str)
    : _conn_str(conn_str)
{
    static const int max_connect_retry_seconds = 60;
    int connect_retry_seconds = 1;
    while (true)
    {
        _conn = lwpg::connectdb(conn_str);
        if (PQstatus(_conn->get()) == CONNECTION_OK)
            break;

        logger->log(LOG_ERROR, "Failed to connect to database: %s", PQerrorMessage(_conn->get()));
        logger->log(LOG_INFO, "Will retry connecting in %i seconds…", connect_retry_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(connect_retry_seconds));
        if (connect_retry_seconds * 2 <= max_connect_retry_seconds) connect_retry_seconds *= 2;
    }

    logger->log(
        LOG_INFO,
        "DB connection established to \x1b[1m%s\x1b[22m on \x1b[1m%s:%s\x1b[22m as \x1b[1m%s\x1b[22m",
        PQdb(_conn->get()), PQhost(_conn->get()), PQport(_conn->get()), PQuser(_conn->get())
    );
}

void CmdQueueRunnerCollection::refresh_queue_list(const std::vector<std::string> &explicit_queue_cmd_classes)
{
    static const int max_retry_seconds = 60;

    std::shared_ptr<lwpg::Result> result;
    int retry_seconds = 1;
    while (true)
    {
        result = lwpg::execParams(_conn, CmdQueue::SELECT_STMT, 1, {}, {lwpg::to_string(explicit_queue_cmd_classes)});

        if (PQresultStatus(result->get())) break;

        logger->log(LOG_ERROR, "Failed to SELECT cmd_queue list: %s", PQresultErrorMessage(result->get()));
        logger->log(LOG_INFO, "Will SELECT again in %i seconds…", retry_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(retry_seconds));
        if (retry_seconds * 2 <= max_retry_seconds) retry_seconds *= 2;
    }

    std::unordered_map<std::string, int> fieldNumbers = lwpg::fnumbers(result);
    for (int i = 0; i < PQntuples(result->get()); i++)
    {
        CmdQueue cmd_queue(result, i, fieldNumbers);

        if (!cmd_queue.is_valid())
        {
            logger->log(LOG_ERROR, cmd_queue.validation_error_message().c_str());
            continue;
        }

        this->add_runner(cmd_queue);
    }
}

void CmdQueueRunnerCollection::listen_for_queue_list_changes()
{
    std::shared_ptr<lwpg::Result> listen_result = lwpg::exec(_conn, "LISTEN cmdq");
    if (PQresultStatus(listen_result->get()) != PGRES_COMMAND_OK)
    {
        logger->log(LOG_ERROR, "Failed to `LISTEN` for `NOTIFY` events on the `cmdq` channel: %s", PQerrorMessage(_conn->get()));
        return;
    }

    struct pollfd fds[] = {
        { PQsocket(_conn->get()), POLLIN | POLLPRI | POLLOUT | POLLERR, 0 }
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
            if (errno == EINTR)
            {
                continue;
            }
        }

        PQconsumeInput(_conn->get());
        while (true)
        {
            std::shared_ptr<lwpg::Notify> notify = lwpg::notifies(_conn);
            if (notify->get() == nullptr)
                break;

            PQconsumeInput(_conn->get());
        }
    }
}

void CmdQueueRunnerCollection::add_runner(const CmdQueue &cmd_queue)
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

void CmdQueueRunnerCollection::join_all_threads()
{
    for (auto &it : _nix_cmd_queue_runners)
        if (it.second.thread.joinable())
            it.second.thread.join();
    for (auto &it : _sql_cmd_queue_runners)
        if (it.second.thread.joinable())
            it.second.thread.join();
}
