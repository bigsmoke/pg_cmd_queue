#include <chrono>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <iostream>
#include <thread>

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>

#include <libpq-fe.h>

#include "pq-raii/libpq-raii.hpp"
#include "logger.h"
#include "cmdqueue.h"
#include "cmdqueuerunner.h"
#include "cmdqueuerunnermanager.h"
#include "nixqueuecmd.h"
#include "sqlqueuecmd.h"
#include "utils.h"

// TODO: rename this file

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
        << "    \x1b[1m--cmd-queue <queue_cmd_class>\x1b[22m     Can be repeated." << std::endl
        << "    \x1b[1m--emit-sigusr1-when-ready\x1b[22m" << std::endl
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

class CmdEnvParseError : std::exception
{
    std::string _message;
public:
    CmdEnvParseError(const std::string &msg)
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

    std::vector<std::string> explicit_queue_cmd_classes;

    std::string conn_str;
    bool emit_sigusr1_when_ready = false;

    try
    {
        if (std::getenv("PG_CMDQD_LOG_LEVEL") != nullptr)
        {
            std::string log_level_from_env = std::getenv("PG_CMDQD_LOG_LEVEL");
            if (StringToLogLevel.count(log_level_from_env) == 0)
                throw CmdLineParseError(std::string("Unrecognized log level: ") + log_level_from_env);
            logger->setLogLevel(StringToLogLevel.at(log_level_from_env));
        }
    }
    catch (const CmdEnvParseError &err)
    {
        std::cerr << "\x1b[31m" << err.what() << "\x1b[0m" << std::endl;
        pg_cmdqd_usage(argv[0], std::cerr);
        exit(2);
    }

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
            else if (std::string(argv[i]) == "--cmd-queue")
            {
                if (i == argc-1)
                    throw CmdLineParseError("Missing \x1b[1m<queue_cmd_class>\x1b[22m argument to \x1b[1m--cmd-queue\x1b[22m option.");
                explicit_queue_cmd_classes.emplace_back(argv[++i]);
            }
            else if (std::string(argv[i]) == "--emit-sigusr1-when-ready")
            {
                emit_sigusr1_when_ready = true;
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

    setenv("PGAPPNAME", basename(argv[0]), 1);

    CmdQueueRunnerManager manager(conn_str, emit_sigusr1_when_ready, explicit_queue_cmd_classes);

    manager.listen_for_queue_list_changes();

    manager.join_all_threads();

    return 0;
}

// vim: set expandtab tabstop=4 softtabstop=4 shiftwidth=4:
