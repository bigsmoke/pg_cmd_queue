// libstd
#include <functional>
#include <stdexcept>
#include <string>
#include <iostream>
#include <thread>

// POSIX
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

// Postgres libpq
#include <postgresql/libpq-fe.h>

#include "lwpg_context.h"
#include "lwpg_results.h"
#include "cmdqueue.h"
#include "queuethread.h"
#include "utils.h"

void pg_cmdqd_usage(char* program_name, std::ostream &stream = std::cout) {
    stream << "Usage:" << std::endl
        << "    " << basename(program_name) << " [ options ] <connection_string" << std::endl
        << "    " << basename(program_name) << " --help | -h" << std::endl
        << std::endl
        << "<connection_string> can be in keyword/value or in URI format, as per the libpq documentation:" << std::endl
        << "    https://www.postgresql.org/docs/current/libpq-connect.html#LIBPQ-CONNSTRING" << std::endl
        << std::endl
        << "Thanks to libpq, most connection parameter values can also be set via environment variables:" << std::endl
        << "    https://www.postgresql.org/docs/current/libpq-envars.html" << std::endl
        << std::endl
        << "Options:" << std::endl
        << "    --config-dir | -d" << std::endl
        << std::endl;
}

int main(int argc, char **argv) {
    std::string conn_str;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            pg_cmdqd_usage(argv[0]);
        }
        else if (i == argc - 1) {
            conn_str.append(argv[1]);
        }
        else {
            std::cerr << "Unrecognized argument/option: " << argv[i] << std::endl;
            pg_cmdqd_usage(argv[0], std::cerr);
        }
    }

    std::cout << "Connecting to database: " << conn_str << std::endl;

    lwpg::Context pg;
    pg.connectdb(conn_str);

    std::cout << "DB connection established." << std::endl;

    lwpg::Results<CmdQueue> cmdQueueResults = pg.query<CmdQueue>(CmdQueue::SELECT);
    std::unordered_map<std::string, CmdQueue> cmdQueues;
    std::unordered_map<std::string, std::thread> threads;

    for (CmdQueue cmdQueue : cmdQueueResults)
    {
        if (!cmdQueue.is_valid())
        {
            continue;
        }

        std::cout << cmdQueue.queue_cmd_class << std::endl;

        cmdQueues[cmdQueue.queue_cmd_class] = std::move(cmdQueue);
    }

    QueueThread qt;
    auto f = std::bind(&QueueThread::run, &qt);
    std::thread t(f);

    if (t.joinable()) {
        t.join();
    }

    return 0;
}

/* vim: set expandtab tabstop=4 softtabstop=4 shiftwidth=4: */
