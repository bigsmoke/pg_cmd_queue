#ifndef LWPG_RESULT_H
#define LWPG_RESULT_H

#include <optional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <libpq-fe.h>

#include "lwpg_conn.h"

namespace lwpg
{

    class Result
    {
        PGresult *result = nullptr;

    public:
        Result(const Result &other) = delete;
        Result(PGresult *res);
        ~Result();

        ExecStatusType getResultStatus() const;
        PGresult *get();
    };

    inline std::shared_ptr<Result>
    exec(
            const std::shared_ptr<Conn> &conn,
            const std::string &command)
    {
        return std::make_shared<lwpg::Result>(PQexec(conn->get(), command.c_str()));
    }

    inline std::shared_ptr<Result>
    execParams(
            const std::shared_ptr<Conn> &conn,
            const std::string &command,
            int nParams,
            const std::optional<std::vector<Oid>> paramTypes = {},
            const std::vector<std::optional<std::string>> &paramValues = {},
            const std::optional<std::vector<int>> &paramLengths = {},
            const std::optional<std::vector<int>> &paramFormats = {},
            int resultFormat = 0)
    {
        std::vector<char *> rawValues; rawValues.reserve(paramValues.size());
        rawValues.clear();
        for (const std::optional<std::string> &paramValue : paramValues)
            rawValues.push_back(paramValue ? const_cast<char*>(paramValue.value().c_str()) : nullptr);

        return std::make_shared<lwpg::Result>(PQexecParams(
                conn->get(),
                command.c_str(),
                nParams,
                paramTypes ? paramTypes.value().data() : nullptr,
                rawValues.data(),
                paramLengths ? paramLengths.value().data() : nullptr,
                paramFormats ? paramFormats.value().data() : nullptr,
                resultFormat
                ));
    }

    inline std::shared_ptr<Result>
    prepare(const std::shared_ptr<Conn> &conn,
            const std::string &stmtName,
            const std::string &query,
            int nParams = 0,
            const std::optional<std::vector<Oid>> paramTypes = {})
    {
        return std::make_shared<lwpg::Result>(PQprepare(
                conn->get(),
                stmtName.c_str(),
                query.c_str(),
                nParams,
                paramTypes ? paramTypes.value().data() : nullptr));
    }

    inline std::shared_ptr<Result>
    execPrepared(
            const std::shared_ptr<Conn> &conn,
            const std::string &stmtName,
            int nParams = 0,
            const std::vector<std::optional<std::string>> &paramValues = {},
            const std::optional<std::vector<int>> &paramLengths = {},
            const std::optional<std::vector<int>> &paramFormats = {},
            int resultFormat = 0)
    {
        std::vector<char *> rawValues; rawValues.reserve(paramValues.size());
        rawValues.clear();
        for (const std::optional<std::string> &paramValue : paramValues)
            rawValues.push_back(paramValue ? const_cast<char*>(paramValue.value().c_str()) : nullptr);

        return std::make_shared<lwpg::Result>(PQexecPrepared(
                conn->get(),
                stmtName.c_str(),
                nParams,
                rawValues.data(),
                paramLengths ? paramLengths.value().data() : nullptr,
                paramFormats ? paramFormats.value().data() : nullptr,
                resultFormat
                ));
    }

    inline std::shared_ptr<Result>
    describePrepared(
            const std::shared_ptr<Conn> &conn,
            const std::string &stmtName)
    {
        return std::make_shared<lwpg::Result>(PQdescribePrepared(conn->get(), stmtName.c_str()));
    }

    inline std::vector<std::string>
    fnames(const std::shared_ptr<Result> &res)
    {
        std::vector<std::string> names;
        names.reserve(PQnfields(res->get()));
        int nfields = PQnfields(res->get());
        for (int i = 0; i < nfields; i++)
            names.push_back(PQfname(res->get(), i));
        return names;
    }

    /**
     * There's no `PQfnumbers()`, function, but `fnumbers()` ought to be guessable enough from the
     * `PQfnumber()` function that does exist.
     */
    inline std::unordered_map<std::string, int>
    fnumbers(const std::shared_ptr<Result> &res)
    {
        int fieldCount = PQnfields(res->get());
        std::unordered_map<std::string, int> map;
        map.reserve(fieldCount);
        for (int i = 0; i < fieldCount; i++)
        {
            std::string value = PQfname(res->get(), i);
            map[value] = i;
        }
        return map;
    }

    inline std::optional<std::string>
    getnullable(const std::shared_ptr<Result> &res, int row_number, const std::string &column_name)
    {
        int column_number = PQfnumber(res->get(), column_name.c_str());
        if (PQgetisnull(res->get(), row_number, column_number) == 1)
            return {};
        return std::string(PQgetvalue(res->get(), row_number, column_number));
    }
}

#endif // LWPG_RESULT_H
