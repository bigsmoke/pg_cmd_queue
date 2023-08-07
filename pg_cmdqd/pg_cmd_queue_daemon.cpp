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

#include "lwpg_context.h"
#include "lwpg_results.h"
#include "cmdqueue.h"
#include "cmdqueuerunner.h"
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

    lwpg::Results<CmdQueue> cmd_queue_results = pg.query<CmdQueue>(CmdQueue::SELECT);
    std::unordered_map<std::string, CmdQueueRunner> cmd_queue_runners;

    for (CmdQueue cmd_queue : cmd_queue_results)
    {
        if (!cmd_queue.is_valid())
        {
            continue;
        }

        std::cout << cmd_queue.queue_cmd_class << std::endl;

        cmd_queue_runners.emplace(std::piecewise_construct, std::forward_as_tuple(cmd_queue.queue_cmd_class), std::forward_as_tuple(cmd_queue));
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

/* vim: set expandtab tabstop=4 softtabstop=4 shiftwidth=4: */
