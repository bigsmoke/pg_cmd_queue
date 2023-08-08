#ifndef LWPG_CONTEXT_H
#define LWPG_CONTEXT_H

#include <vector>

#include "postgresql/libpq-fe.h"

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
        std::shared_ptr<Conn> conn;

    public:
        void connectdb(const std::string &conninfo);

        template<typename T>
        lwpg::Results<T> query(const std::string &query)
        {
            if (!this->conn)
                throw std::runtime_error("No connection");

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
        lwpg::Results<T> query(const std::string &query, const std::vector<std::string> &params)
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

        void exec(const std::string &query);
        void exec(const std::string &query, const std::vector<std::string> &params);
    };

}

#endif // LWPG_CONTEXT_H
