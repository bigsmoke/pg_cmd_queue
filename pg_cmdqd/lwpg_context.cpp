#include "lwpg_context.h"
#include "lwpg_result_iterator.h"

std::shared_ptr<lwpg::Conn> lwpg::Context::connectdb(const std::string &conninfo)
{
    conn = std::make_shared<lwpg::Conn>(PQconnectdb(conninfo.c_str()));

    if (PQstatus(conn->get()) != CONNECTION_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }

    return conn;
}

void lwpg::Context::exec(const std::string &query)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    logger->log(LOG_DEBUG5, "lwpg::Context::exec() %s", query.c_str());
    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(conn->get(), query.c_str()));

    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}

void lwpg::Context::exec(const std::string &query, const std::vector<std::optional<std::string>> &params)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    std::vector<char *> values(params.size());
    values.clear();
    for (const std::optional<std::string> &param : params)
        values.push_back(param ? const_cast<char*>(param.value().c_str()) : nullptr);

    logger->log(LOG_DEBUG5, "lwpg::Context::exec() %s", query.c_str());
    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexecParams(conn->get(),
        query.c_str(), values.size(),  nullptr, values.data(), nullptr, nullptr, 0) );

    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}

void lwpg::Context::prepare(const std::string &stmt_name, const std::string &statement, int n_params)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQprepare(conn->get(),
                stmt_name.c_str(), statement.c_str(), n_params,  nullptr));

    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }
}

std::unordered_map<std::string, int>
lwpg::Context::describe_prepared_field_mappings(const std::string &stmt_name)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQdescribePrepared(conn->get(),
                stmt_name.c_str()));

    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }

    int fieldCount = PQnfields(result->get());
    std::unordered_map<std::string, int> fieldMappings;
    for (int i = 0; i < fieldCount; i++)
    {
        std::string value = PQfname(result->get(), i);
        fieldMappings[value] = i;
    }
    return fieldMappings;
}

std::shared_ptr<lwpg::Result>
lwpg::Context::exec_prepared(const std::string &stmt_name, const std::vector<std::optional<std::string>> &params)
{
    if (!this->conn)
        throw std::runtime_error("No connection");

    const char *values[params.size()];
    int i = 0;
    for (const std::optional<std::string> &param : params)
    {
        values[i++] = param ? param.value().c_str() : nullptr;
    }

    std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexecPrepared(conn->get(),
        stmt_name.c_str(), params.size(), values, nullptr, nullptr, 0) );

    if (result->getResultStatus() != PGRES_COMMAND_OK)
    {
        std::string error(PQerrorMessage(conn->get()));
        throw std::runtime_error(error);
    }

    return result;
}

int lwpg::Context::socket() const
{
    return PQsocket(this->conn->get());
}
