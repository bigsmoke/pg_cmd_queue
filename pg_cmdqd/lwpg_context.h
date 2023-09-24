#ifndef LWPG_CONTEXT_H
#define LWPG_CONTEXT_H

#include <vector>

#include <libpq-fe.h>

#include "logger.h"
#include "lwpg_conn.h"
#include "lwpg_result.h"
#include "lwpg_results.h"

namespace lwpg
{

    /**
     * @brief Context is where you run queries and such. Its connection is a shared pointer because it may need to be passed around, to iterators and such.
     */
    class Context
    {
        std::shared_ptr<Conn> conn;  // TODO: Make public or prefix with underscore
        Logger *logger = Logger::getInstance();

    public:
        std::shared_ptr<Conn> connectdb(const std::string &conninfo);

        template<typename T>
        lwpg::Results<T> query(const std::string &query)
        {
            if (!this->conn)
                throw std::runtime_error("No connection");

            logger->log(LOG_DEBUG5, "lwpg::Context::query() %s", query.c_str());
            std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(conn->get(), query.c_str()));

            if (result->getResultStatus() != PGRES_TUPLES_OK)
            {
                std::string error(PQerrorMessage(conn->get()));
                throw std::runtime_error(error);
            }

            int rowCount = PQntuples(result->get());
            int fieldCount = PQnfields(result->get());
            lwpg::ResultIterator<T> begin(result, conn, rowCount, fieldCount);

            lwpg::Results<T> results(begin, rowCount);
            return results;
        }

        template<typename T>
        std::optional<T> query1(const std::string &query)
        {
            if (!this->conn)
                throw std::runtime_error("No connection");

            logger->log(LOG_DEBUG5, "lwpg::Context::query1() %s", query.c_str());
            std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexec(conn->get(), query.c_str()));

            if (result->getResultStatus() != PGRES_TUPLES_OK)
            {
                std::string error(PQerrorMessage(conn->get()));
                throw std::runtime_error(error);
            }

            int row_count = PQntuples(result->get());
            if (row_count > 1)
                throw std::runtime_error("Too many rows; only one row expected");
            if (row_count == 0)
                return std::nullopt;

            int fieldCount = PQnfields(result->get());
            std::unordered_map<std::string, int> fieldMappings;
            for (int i = 0; i < fieldCount; i++)
            {
                std::string value = PQfname(result->get(), i);
                fieldMappings[value] = i;
            }

            T t(result, 0, fieldMappings);
            return t;
        }

        template<typename T>
        lwpg::Results<T> query(const std::string &query, const std::vector<std::optional<std::string>> &params)
        {
            if (!this->conn)
                throw std::runtime_error("No connection");

            const char *values[params.size()];
            int i = 0;
            for (const std::optional<std::string> &param : params)
            {
                values[i++] = param ? param.value().c_str() : nullptr;
            }

            logger->log(LOG_DEBUG5, "lwpg::Context::query() %s", query.c_str());
            std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexecParams(conn->get(),
                query.c_str(), params.size(),  nullptr, values, nullptr, nullptr, 0) );

            if (result->getResultStatus() != PGRES_TUPLES_OK)
            {
                std::string error(PQerrorMessage(conn->get()));
                throw std::runtime_error(error);
            }

            int rowCount = PQntuples(result->get());
            int fieldCount = PQnfields(result->get());
            lwpg::ResultIterator<T> begin(result, conn, rowCount, fieldCount);

            lwpg::Results<T> results(begin, rowCount);
            return results;
        }

        template<typename T>
        std::optional<T> query1(const std::string &query, const std::vector<std::optional<std::string>> &params)
        {
            if (!this->conn)
                throw std::runtime_error("No connection");

            const char *values[params.size()];
            int i = 0;
            for (const std::optional<std::string> &param : params)
            {
                values[i++] = param ? param.value().c_str() : nullptr;
            }

            logger->log(LOG_DEBUG5, "lwpg::Context::query1() %s", query.c_str());
            std::shared_ptr<lwpg::Result> result = std::make_shared<lwpg::Result>(PQexecParams(conn->get(),
                query.c_str(), params.size(),  nullptr, values, nullptr, nullptr, 0) );

            if (result->getResultStatus() != PGRES_TUPLES_OK)
            {
                std::string error(PQerrorMessage(conn->get()));
                throw std::runtime_error(error);
            }

            int rowCount = PQntuples(result->get());
            if (rowCount > 1)
                throw std::runtime_error("Too many rows; only one row expected");
            if (rowCount == 0)
                return std::nullopt;

            int fieldCount = PQnfields(result->get());
            std::unordered_map<std::string, int> fieldMappings;
            for (int i = 0; i < fieldCount; i++)
            {
                std::string value = PQfname(result->get(), i);
                fieldMappings[value] = i;
            }

            T t(result, 0, fieldMappings);
            return t;
        }

        void exec(const std::string &query);
        void exec(const std::string &query, const std::vector<std::optional<std::string>> &params);
        void prepare(const std::string &stmt_name, const std::string &statement, int n_params = 0);
        std::unordered_map<std::string, int> describe_prepared_field_mappings(const std::string &stmt_name);

        std::shared_ptr<lwpg::Result> exec_prepared(const std::string &stmt_name, const std::vector<std::optional<std::string>> &params);

        int socket() const;
    };
}

#endif // LWPG_CONTEXT_H
