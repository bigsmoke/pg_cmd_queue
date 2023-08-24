#include <functional>
#include <stdexcept>
#include <string>
#include <iostream>
#include <thread>

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include <postgresql/libpq-fe.h>

#include "logger.h"
#include "lwpg_context.h"
#include "lwpg_results.h"
#include "cmdqueue.h"
#include "cmdqueuerunner.h"
#include "utils.h"

void pg_cmdqd_usage(char* program_name, std::ostream &stream = std::cout)
{
    stream << "Usage:" << std::endl
        << "    \x1b[1m" << basename(program_name) << "\x1b[22m [ \x1b[1moptions\x1b[22m ] \x1b[1m<connection_string>\x1b[22m" << std::endl
        << "    \x1b[1m" << basename(program_name) << "\x1b[22m \x1b[1m--help\x1b[22m | \x1b[1m-h\x1b[22m" << std::endl
        << std::endl
        << "\x1b[1m<connection_string>\x1b[22m" << std::endl
        << "    Can be in keyword/value or in URI format, as per the libpq documentation:" << std::endl
        << "    \x1b[4mhttps://www.postgresql.org/docs/current/libpq-connect.html#LIBPQ-CONNSTRING\x1b[24m" << std::endl
        << std::endl
        << "    Thanks to libpq, most connection parameter values can also be set via" << std::endl
        << "    environment variables:"
        << " \x1b[4mhttps://www.postgresql.org/docs/current/libpq-envars.html\x1b[24m" << std::endl
        << std::endl
        << "Options:" << std::endl
        << "    \x1b[1m--log-level <log_level>\x1b[22m" << std::endl
        << std::endl;
}

class CmdLineParseError : std::exception
{
    std::string _message;

public:
    CmdLineParseError(const std::string &msg)
        : _message(msg) {}

    const char *what() const noexcept override
    {
        return _message.data();
    }
};

int main(int argc, char **argv)
{
    Logger *logger = Logger::getInstance();
    logger->setLogLevel(LOG_INFO);

    std::string conn_str;
    try
    {
        for (int i = 1; i < argc; i++)
        {
            if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h")
            {
                pg_cmdqd_usage(argv[0]);
                exit(EXIT_SUCCESS);
            }
            else if (std::string(argv[i]) == "--log-level")
            {
                if (i == argc-1)
                    throw CmdLineParseError("Missing \x1b[1m<log_level>\x1b[22m argument to \x1b[1m--log-level\x1b[22m option.");
                if (StringToLogLevel.count(argv[++i]) == 0)
                    throw CmdLineParseError(std::string("Unrecognized log level: ") + argv[i]);
                logger->setLogLevel( StringToLogLevel.at(argv[i]) );
            }
            else if (i == argc - 1)
            {
                // Assume that the last argument is the connection string.
                conn_str.append(argv[1]);
            }
            else
                throw CmdLineParseError(std::string("Unrecognized argument/option: ") + argv[i]);
        }
    }
    catch (const CmdLineParseError &err)
    {
        std::cerr << "\x1b[31m" << err.what() << "\x1b[0m" << std::endl;
        pg_cmdqd_usage(argv[0], std::cerr);
        exit(2);
    }

    if (!conn_str.empty())
        logger->log(LOG_INFO, "Connecting to database: \x1b[1m%s\x1b[0m", conn_str.c_str());
    else
        logger->log(LOG_INFO, "No connectiong string given; letting libpq figure out what to do from the \x1b[1mPG*\x1b[0m environment variables…");

    lwpg::Context pg;
    std::shared_ptr<lwpg::Conn> conn = pg.connectdb(conn_str);

    logger->log(
        LOG_INFO,
        "DB connection established to \x1b[1m%s\x1b[22m on \x1b[1m%s:%s\x1b[22m as \x1b[1m%s\x1b[22m",
        PQdb(conn->get()), PQhost(conn->get()), PQport(conn->get()), PQuser(conn->get())
    );

    lwpg::Results<CmdQueue> cmd_queue_results = pg.query<CmdQueue>(CmdQueue::SELECT_STMT);
    std::unordered_map<std::string, CmdQueueRunner> cmd_queue_runners;

    for (CmdQueue cmd_queue : cmd_queue_results)
    {
        if (!cmd_queue.is_valid())
        {
            std::cerr << cmd_queue.validation_error_message();
            continue;
        }

        cmd_queue_runners.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(cmd_queue.queue_cmd_relname),
            std::forward_as_tuple(cmd_queue, conn_str)
        );
    }

    for (auto &it : cmd_queue_runners)
    {
        if (it.second.thread.joinable())
        {
            it.second.thread.join();
        }
    }

    return 0;
}

// vim: set expandtab tabstop=4 softtabstop=4 shiftwidth=4:
