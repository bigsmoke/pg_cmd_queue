#include <signal.h>

#include "pq_cmdqd_utils.h"
#include "sigstate.h"

void maintain_connection(const std::string &conn_str, std::shared_ptr<PG::conn> &conn)
{
    static Logger *logger = Logger::getInstance();

    if (conn and PQ::status(conn) == CONNECTION_OK)
        return;

    static const int max_connect_retry_seconds = 60;

    if (not conn)
    {
        if (not conn_str.empty())
            logger->log(LOG_INFO, "Connecting to database: \x1b[1m%s\x1b[22m", conn_str.c_str());
        else
            logger->log(LOG_DEBUG1, "No connectiong string given; letting libpq figure out what to do from the \x1b[1mPG*\x1b[22m environment variables…");

        int connect_retry_seconds = 1;
        while (not sig_num_received({SIGQUIT, SIGTERM, SIGINT}))
        {
            conn = PQ::connectdb(conn_str);
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
        while (not sig_num_received({SIGQUIT, SIGTERM, SIGINT}))
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
