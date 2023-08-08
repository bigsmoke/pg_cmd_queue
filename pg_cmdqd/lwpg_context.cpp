#include "lwpg_context.h"
#include "lwpg_result_iterator.h"

void lwpg::Context::connectdb(const std::string &conninfo)
{
    conn = std::make_shared<lwpg::Conn>(PQconnectdb(conninfo.c_str()));

    if (PQstatus(conn->get()) != CONNECTION_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }

    // Secure search path. The docs suggest this. TODO: talk to Rowan.
    lwpg::Result res(PQexec(conn->get(), "SELECT pg_catalog.set_config('search_path', '', false)"));

    if (res.getResultStatus() != PGRES_TUPLES_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}

void lwpg::Context::exec(const std::string &query)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(conn->get(), query.c_str()));

    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}

void lwpg::Context::exec(const std::string &query, const std::vector<std::string> &params)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    const char *values[params.size()];
    int i = 0;
    for (const std::string &param : params)
    {
        values[i++] = param.c_str();
    }

    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexecParams(conn->get(),
        query.c_str(), params.size(),  nullptr, values, nullptr, nullptr, 0) );


    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }

    if (result->getResultStatus() != PGRES_TUPLES_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}
