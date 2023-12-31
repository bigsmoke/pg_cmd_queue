#include "lwpg_context.h"
#include "lwpg_result_iterator.h"

void LWPGcontext::connectdb(const std::string &conninfo)
{
    conn = std::make_shared<LWPGconn>(PQconnectdb(conninfo.c_str()));

    if (PQstatus(conn->get()) != CONNECTION_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }

    // Secure search path. The docs suggest this. TODO: talk to Rowan.
    LWPGresult res(PQexec(conn->get(), "SELECT pg_catalog.set_config('search_path', '', false)"));

    if (res.getResultStatus() != PGRES_TUPLES_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}

void LWPGcontext::exec(const std::string query)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    std::shared_ptr<LWPGresult> result = std::make_shared<LWPGresult>(PQexec(conn->get(), query.c_str()));

    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}
