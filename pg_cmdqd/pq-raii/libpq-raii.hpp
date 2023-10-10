#ifndef LIBPQ_RAII_HPP
#define LIBPQ_RAII_HPP

#include "postgres_ext.h"
#include <cassert>
#include <optional>
#include <map>
#include <memory>
#include <unordered_map>
#include <regex>
#include <string>
#include <vector>

#include <libpq-fe.h>

/**
 * This library, rather than pouring the OO flavor of the day on top of libpq, really does little more than
 * wrapping the `PG…` pointer types in RAII wrappers.  The existing libpq function and type names are
 * used, except that the `PQ` function prefix is replaced with the `PQ` namespace and the `PG` struct
 * prefix is replaced with the `PG` namespace.  The rest is pretty discoverable if you know C++, the STL
 * and libpq.  Nothing new to learn here.  You're welcome.
 *
 * Okay, there is a _little_ more.  There are a bunch of functions to aid in working with PostgreSQL types.
 * Nothing too fancy.  The names all start with:
 *
 *   - a `from_` prefix, for functions parsing something out of a PostgreSQL text representation; or
 *   - an `as_` prefix, for functions serializing something into the canonical PostgreSQL text form.
 *     (The `to_` prefix was considered too confusing due to its unrelated use in the STL.)
 *
 * The functions in the `PQ` namespace that share approx. the same name as the wrapped functions from libpq
 * don't throw any exceptions ever.  The extra type conversion functions _might_.
 *
 * Finally, there is an iterator, if you insist.  It allows you to walk through the tuples in a `PG::result`.
 */

/**
 * The `PG` namespace contains RAII wrappers for pointers to the structeres that in `libpq` are similarly
 * prefixed by `PG`, plus some classes that are specific to `libpq-raii`.
 */
namespace PG
{
    class conn
    {
        PGconn *raw_conn_ptr = nullptr;

    public:
        conn(const conn &other) = delete;

        inline conn(PGconn *raw_conn_ptr)
            : raw_conn_ptr(raw_conn_ptr)
        {}

        inline ~conn()
        {
            if (!raw_conn_ptr)
                return;

            PQfinish(raw_conn_ptr);
        }

        inline PGconn *get()
        {
            return this->raw_conn_ptr;
        }
    };

    class result
    {
        PGresult *res = nullptr;

    public:
        result(const result &other) = delete;

        inline result(PGresult *res)
            : res(res)
        {}

        inline ~result()
        {
            if (!this->res)
                return;

            PQclear(this->res);
        }

        inline PGresult *get()
        {
            return this->res;
        }

        /*
        inline void status_or_throw(const ExecStatusType &expectedStatus)
        {
            if (PQresultStatus(this->res))
        }
        */
    };

    class notify
    {
        PGnotify *_raw_notify_ptr = nullptr;

    public:
        notify(const notify &other) = delete;

        notify(PGnotify *raw_notify_ptr)
            : _raw_notify_ptr(raw_notify_ptr)
        {}

        ~notify()
        {
            if (!this->_raw_notify_ptr)
                return;

            PQfreemem(this->_raw_notify_ptr);
        }

        PGnotify *get()
        {
            return this->_raw_notify_ptr;
        }

        std::string relname() const
        {
            return std::string(this->_raw_notify_ptr->relname);
        }

        int be_pid() const
        {
            return this->_raw_notify_ptr->be_pid;
        }

        std::string extra() const
        {
            return std::string(this->_raw_notify_ptr->extra);
        }
    };

    /**
     * \brief The tuple_iterator knows how to iterate over rows and use operatior*() to construct an object
     * of the templated type.
     *
     * It's important that your type does not throw exceptions in its constructor. Also, the operator*()
     * doesn't return a reference, because that's impossible, though the objected created is a local copy,
     * which can be moved.
     *
     * The snake case name of this class is intentionally a bit more towards the STL conventions than the
     * libpq conventions, though libpq mixes styles _a lot_.
     */
    template<typename T>
    class tuple_iterator
    {
        std::shared_ptr<PG::result> result;
        std::shared_ptr<PG::conn> conn;
        std::unordered_map<std::string, int> fieldMappings;
        int row = 0;
        int rowCount = 0;
        int fieldCount = 0;

    public:
        tuple_iterator() = delete;

        /**
         * \brief basically the constructor for the end iterator.
         */
        inline tuple_iterator(int rowCount):
            rowCount(rowCount)
        {}

        inline tuple_iterator(
                std::shared_ptr<PG::result> &result,
                std::shared_ptr<PG::conn> &conn,
                int rowCount,
                int fieldCount)
        :   result(result),
            conn(conn),
            rowCount(rowCount),
            fieldCount(fieldCount)
        {
            fieldMappings.reserve(fieldCount);
            for (int i = 0; i < fieldCount; i++)
            {
                std::string value = PQfname(result->get(), i);
                fieldMappings[value] = i;
            }
        }

        inline bool operator!=(tuple_iterator &rhs)
        {
            return this->row != rhs.row;
        }

        inline tuple_iterator &operator++(int)
        {
            row++;
            return *this;
        }

        inline tuple_iterator &operator++()
        {
            return operator++(0);
        }

        T operator*()
        {
            if (row >= rowCount)
                std::runtime_error("Trying to dereference invalid iterator");

            T t(result, row, fieldMappings);
            return t;
        }
    };

    /**
     * @brief Container type for iterating through tuples in a result.
     *
     * This class is also the RAII place to deal with finishing async operations. Async operations need to
     * continue until there's nothing more to read, otherwise the connection can't be used for something else.
     * The destructor would be the best place. It would need a shared pointer to a Result then.
     *
     */
    template<typename T>
    class tuples
    {
        PG::tuple_iterator<T> _begin;
        int rowCount = 0;

    public:
        inline tuples<T>(PG::tuple_iterator<T> &begin, int rowCount) :
            _begin(begin),
            rowCount(rowCount)
        {}

        inline tuple_iterator<T> begin()
        {
            return _begin;
        }

        inline tuple_iterator<T> end()
        {
            return PG::tuple_iterator<T>(this->rowCount);
        }
    };
}

/**
 * The `PQ` namespace contains RAII wrappers for functions that in `libpq` are similarly prefixed by `PQ`,
 * plus some additional functions that make sense in the context of C++ and the STL.
 *
 * As an extra, there are some type conversion functions as well, which are missing in `libpq` and are much
 * easier to do with the STL than in raw C(++).
 */
namespace PQ
{
    // Where possible, functions are defined in the same order as they appear in the libpq documentation:
    // https://www.postgresql.org/docs/current/libpq.html

    inline std::shared_ptr<PG::conn> connectdb(const std::string &conninfo)
    {
        return std::make_shared<PG::conn>(PQconnectdb(conninfo.c_str()));
    }

    // TODO: Actually try this function, even just once.
    inline std::shared_ptr<PG::conn>
    connectdbParams(
            const std::map<std::string, std::string> &keyword_values,
            int expand_dbnname
        )
    {
        std::vector<std::vector<char>> keys;
        std::vector<std::vector<char>> values;
        keys.reserve(keyword_values.size());
        values.reserve(keyword_values.size());
        for (auto kv: keyword_values)
        {
            keys.emplace_back(kv.first.begin(), kv.first.end());
            values.emplace_back(kv.second.begin(), kv.second.end());
        }

        return std::make_shared<PG::conn>(PQconnectdbParams(
                (const char * const *)keys.data(),
                (const char * const *)values.data(),
                expand_dbnname
                ));
    }

    inline void reset(std::shared_ptr<PG::conn> &conn)
    {
        PQreset(conn->get());
    }

    inline ConnStatusType status(const std::shared_ptr<PG::conn> &conn)
    {
        return PQstatus(conn->get());
    }

    inline std::string
    errorMessage(const std::shared_ptr<PG::conn> &conn)
    {
        return std::string(PQerrorMessage(conn->get()));
    }

    inline PGTransactionStatusType
    transactionStatus(const std::shared_ptr<PG::conn> &conn)
    {
        return PQtransactionStatus(conn->get());
    }

    inline int socket(const std::shared_ptr<PG::conn> &conn)
    {
        return PQsocket(conn->get());
    }

    inline std::shared_ptr<PG::result>
    exec(
            const std::shared_ptr<PG::conn> &conn,
            const std::string &command
        )
    {
        return std::make_shared<PG::result>(PQexec(conn->get(), command.c_str()));
    }

    inline std::shared_ptr<PG::result>
    execParams(
            const std::shared_ptr<PG::conn> &conn,
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

        return std::make_shared<PG::result>(PQexecParams(
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

    inline std::shared_ptr<PG::result>
    prepare(const std::shared_ptr<PG::conn> &conn,
            const std::string &stmtName,
            const std::string &query,
            int nParams = 0,
            const std::optional<std::vector<Oid>> paramTypes = {})
    {
        return std::make_shared<PG::result>(PQprepare(
                conn->get(),
                stmtName.c_str(),
                query.c_str(),
                nParams,
                paramTypes ? paramTypes.value().data() : nullptr));
    }

    inline std::shared_ptr<PG::result>
    execPrepared(
            const std::shared_ptr<PG::conn> &conn,
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

        return std::make_shared<PG::result>(PQexecPrepared(
                conn->get(),
                stmtName.c_str(),
                nParams,
                rawValues.data(),
                paramLengths ? paramLengths.value().data() : nullptr,
                paramFormats ? paramFormats.value().data() : nullptr,
                resultFormat
                ));
    }

    inline std::shared_ptr<PG::result>
    describePrepared(
            const std::shared_ptr<PG::conn> &conn,
            const std::string &stmtName)
    {
        return std::make_shared<PG::result>(PQdescribePrepared(conn->get(), stmtName.c_str()));
    }

    inline ExecStatusType
    resultStatus(const std::shared_ptr<PG::result> &res)
    {
        return PQresultStatus(res->get());
    }

    inline std::string
    resultErrorMessage(const std::shared_ptr<PG::result> &res)
    {
        return std::string(PQresultErrorMessage(res->get()));
    }

    /**
     * Convert `char*` to either a string, if it has characters, or `std::nullopt` if it just has a `nullptr`.
     */
    inline std::optional<std::string>
    from_text(const char *c_str)
    {
        if (c_str == nullptr)
            return {};

        return std::string(c_str);
    }

    /**
     * Get _all_ the error fields rather than just one, as `libpq`'s PQresultErrorField() does.
     *
     * The `PG_DIAG_*` macro definitions from the `postgres_ext.h` header are character literals, which
     * makes them `int`s in C and `char`s in C++.  Hence the choice for `char` rather than `int` as the key
     * type in this _C++_ library.
     *
     * The return type is a `map`, not an `unordered_map`, because the library writer had a specific
     * application in mind that needed a composite value with the error fields ordered in the precise sequence
     * that they are defined in
     * [`postgres_ext.h`](https://github.com/postgres/postgres/blob/master/src/include/postgres_ext.h).
     */
    inline std::map<char, std::optional<std::string>>
    resultErrorFields(const std::shared_ptr<PG::result> &res)
    {
        return std::map<char, std::optional<std::string>>({
            {PG_DIAG_SEVERITY, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_SEVERITY))},
            {PG_DIAG_SEVERITY_NONLOCALIZED, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_SEVERITY_NONLOCALIZED))},
            {PG_DIAG_SQLSTATE, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_SQLSTATE))},
            {PG_DIAG_MESSAGE_PRIMARY, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_MESSAGE_PRIMARY))},
            {PG_DIAG_MESSAGE_DETAIL, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_MESSAGE_DETAIL))},
            {PG_DIAG_MESSAGE_HINT, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_MESSAGE_HINT))},
            {PG_DIAG_STATEMENT_POSITION, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_STATEMENT_POSITION))},
            {PG_DIAG_INTERNAL_POSITION, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_INTERNAL_POSITION))},
            {PG_DIAG_INTERNAL_QUERY, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_INTERNAL_QUERY))},
            {PG_DIAG_CONTEXT, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_CONTEXT))},
            {PG_DIAG_SCHEMA_NAME, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_SCHEMA_NAME))},
            {PG_DIAG_TABLE_NAME, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_TABLE_NAME))},
            {PG_DIAG_COLUMN_NAME, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_COLUMN_NAME))},
            {PG_DIAG_DATATYPE_NAME, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_DATATYPE_NAME))},
            {PG_DIAG_CONSTRAINT_NAME, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_CONSTRAINT_NAME))},
            {PG_DIAG_SOURCE_FILE, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_SOURCE_FILE))},
            {PG_DIAG_SOURCE_LINE, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_SOURCE_LINE))},
            {PG_DIAG_SOURCE_FUNCTION, PQ::from_text(PQresultErrorField(res->get(), PG_DIAG_SOURCE_FUNCTION))},
        });
    }

    inline int
    ntuples(const std::shared_ptr<PG::result> &res)
    {
        return PQntuples(res->get());
    }

    inline std::vector<std::string>
    fnames(const std::shared_ptr<PG::result> &res)
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
    fnumbers(const std::shared_ptr<PG::result> &res)
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

    inline bool consumeInput(const std::shared_ptr<PG::conn> &conn)
    {
        return (bool)PQconsumeInput(conn->get());
    }

    inline std::shared_ptr<PG::notify>
    notifies(const std::shared_ptr<PG::conn> &conn)
    {
        return std::make_shared<PG::notify>(PQnotifies(conn->get()));
    }

    template <typename K, typename V>
    inline std::vector<V>
    values_to_vector(const std::map<K,V> &m)
    {
        std::vector<V> values;
        values.reserve(m.size());
        for (const std::pair<K,V> &p : m)
            values.push_back(p.second);

        return values;

        /* This needs a higher C++
        std::transform(
                m.begin(),
                m.end(),
                std::back_inserter(values),
                [](const typename std::map<K,V>::value_type &pair){ return pair.second; });
        */
    }

    inline std::string
    getvalue(const std::shared_ptr<PG::result> &res, int row_number, int column_number)
    {
        return std::string(PQgetvalue(res->get(), row_number, column_number));
    }

    inline std::string
    getvalue(const std::shared_ptr<PG::result> &res, int row_number, const std::string &column_name)
    {
        int column_number = PQfnumber(res->get(), column_name.c_str());
        return getvalue(res, row_number, column_number);
    }

    inline bool
    getisnull(const std::shared_ptr<PG::result> &res, int row_number, int column_number)
    {
        return PQgetisnull(res->get(), row_number, column_number) == 1;
    }

    inline std::optional<std::string>
    getnullable(const std::shared_ptr<PG::result> &res, int row_number, int column_number)
    {
        if (PQgetisnull(res->get(), row_number, column_number) == 1)
            return {};
        return std::string(PQgetvalue(res->get(), row_number, column_number));
    }

    inline std::optional<std::string>
    getnullable(const std::shared_ptr<PG::result> &res, int row_number, const std::string &column_name)
    {
        int column_number = PQfnumber(res->get(), column_name.c_str());
        return getnullable(res, row_number, column_number);
    }

    inline std::string
    double_quote(const std::string &unquoted)
    {
        static std::regex re("\"|\\\\");

        return std::string("\"") + std::regex_replace(unquoted, re, "\\$&") + "\"";
    }

    inline std::optional<std::string>
    as_text(const std::optional<int> &i)
    {
        if (not i.has_value())
            return {};

        return std::to_string(i.value());
    }

    inline std::optional<std::string>
    as_text(const char *c_str)
    {
        if (c_str == nullptr)
            return {};

        return std::string(c_str);
    }

    /**
     * Convert a PostgreSQL array in text form to a vector of strings representing the array member values.
     */
    inline std::vector<std::string>
    from_text_array(const std::string &input)
    {
        // TODO: UTF-8 wierdnesses; and check Postgres' own parsing
        std::vector<std::string> result;
        int depth = 0;
        bool in_quotes = false;
        std::string::size_type start_pos = 1;
        std::string::size_type end_pos = 0;
        std::string::size_type next_start_pos = 0;

        assert(input.at(0) == '{');
        assert(input.back() == '}');

        for (std::string::size_type i = 0; i < input.size();)
        {
            if (input.at(i) == '{' && not in_quotes)
            {
                if (++depth == 1)
                {
                    start_pos = i+1;
                }
            }
            else if (input.at(i) == '}' && not in_quotes)
            {
                if (depth-- == 1 && end_pos == 0)
                {
                    end_pos = i-1;
                    next_start_pos = 0;
                }
            }
            else if (input.at(i) == ',' && not in_quotes)
            {
                end_pos = i-1;
                next_start_pos = i+1;
            }
            // We don't have to worry about `i-1 < 0`, because `input.at(0) == '{'`.
            else if (input.at(i) == '"' && input.at(i-1) != '\\')
            {
                if (in_quotes)
                {
                    end_pos = i-1;
                    next_start_pos = i+2;
                    in_quotes = false;
                }
                else
                {
                    start_pos = i+1;
                    in_quotes = true;
                }
            }

            if (start_pos > 0 and end_pos > 0)
            {
                result.push_back(input.substr(start_pos, end_pos-start_pos+1));

                if (next_start_pos == 0) break;

                end_pos = 0;
                start_pos = next_start_pos;
                i = start_pos;
            }
            else
                i++;
        }

        return result;
    }

    inline std::string
    as_text_array(const std::vector<std::optional<std::string>> &arrayish)
    {
        std::string array_text = "{";

        int i = 0;
        for (const std::optional<std::string> &member : arrayish)
        {
            if (i++ > 0)
                array_text += ",";

            if (member)
                array_text += double_quote(*member);
            else
                array_text += "NULL";
        }

        array_text += "}";

        return array_text;
    }

    inline std::string
    as_text_array(const std::vector<std::string> &arrayish)
    {
        std::string array_text = "{";

        int i = 0;
        for (const std::string &member : arrayish)
        {
            if (i++ > 0)
                array_text += ",";

            array_text += double_quote(member);
        }

        array_text += "}";

        return array_text;
    }

    template<class I,
             typename = std::enable_if<
                 std::is_same<typename std::iterator_traits<I>::value_type, std::string>::value>>
    inline std::string
    as_text_composite_value(I first, I last)
    {
        std::string composite_value = "(";

        for (auto i = first; i != last; i++)
        {
            if (i != first)
                composite_value += ",";
            composite_value += double_quote(*i);
        }

        composite_value += ")";

        return composite_value;
    }

    inline std::string
    as_text_composite_value(std::initializer_list<std::string> field_values)
    {
        return as_text_composite_value(field_values.begin(), field_values.end());
    }

    template <class Container,
              typename = std::enable_if<std::is_same<typename Container::value_type,
                                                     std::optional<std::string>>::value>>
    inline std::string
    as_text_composite_value(const Container &field_values)
    {
        std::string composite_value = "(";

        int i = 0;
        for (const std::optional<std::string> &field_value : field_values)
        {
            if (i++ > 0)
                composite_value += ",";
            composite_value += field_value ? double_quote(field_value.value()) : "NULL";
        }

        composite_value += ")";

        return composite_value;
    }

    inline std::string
    as_text_composite_value(std::initializer_list<std::optional<std::string>> field_values)
    {
        std::string composite_value = "(";

        int i = 0;
        for (const std::optional<std::string> &field_value : field_values)
        {
            if (i++ > 0)
                composite_value += ",";
            composite_value += field_value ? double_quote(field_value.value()) : "NULL";
        }

        composite_value += ")";

        return composite_value;
    }

    inline std::vector<std::optional<std::string>>
    from_text_composite_value(const std::string &input)
    {
        std::vector<std::optional<std::string>> result;
        int depth = 0;
        bool in_quotes = false;
        int item_num = 0;
        std::string::size_type start_pos = 0;
        std::string::size_type end_pos = 0;

        assert(input.at(0) == '(');
        assert(input.back() == ')');

        for (std::string::size_type i = 0; i < input.size(); ++i)
        {
            if (input.at(i) == '(' && not in_quotes)
            {
                if (++depth == 1)
                {
                    item_num++;
                    start_pos = i+1;
                }
            }
            else if (input.at(i) == ')' && not in_quotes)
            {
                if (depth-- == 1 && end_pos == 0)
                {
                    end_pos = i-1;
                }
            }
            else if (input.at(i) == ',' && not in_quotes)
            {
                end_pos = i-1;
            }
            // We don't have to worry about `i-1 < 0`, because `input.at(i) == '{'`.
            else if (input.at(i) == '"' && input.at(i-1) != '\\')
            {
                if (in_quotes)
                {
                    end_pos = i-1;
                    in_quotes = false;
                }
                else
                {
                    start_pos = i+1;
                    in_quotes = true;
                }
            }

            if (start_pos > 0 && end_pos > 0)
            {
                if (end_pos == start_pos - 1)
                    result.push_back({});
                else
                    result.push_back(input.substr(start_pos, end_pos-start_pos+1));
                start_pos = i+1;
                end_pos = 0;
            }
        }

        return result;
    }

    inline std::string
    unescape_hstore_text(const std::string &escaped)
    {
        std::string unescaped("");
        for (std::string::size_type i = 0; i < escaped.size(); ++i)
        {
            // Skip past the escape character, unless it is the _escaped_ escaped character itself.
            if (escaped[i] == '\\' and (i == 0 or escaped[i-1] != '\\')) i++;

            unescaped.push_back(escaped[i]);
        }
        return unescaped;
    }

    /**
     * Parse Postgres `hstore` string to an `unordered_map` of each item in the `hstore`.
     */
    inline std::unordered_map<std::string, std::string>
    from_text_hstore(const std::string &input)
    {
        std::unordered_map<std::string, std::string> result;

        bool in_quotes = false;
        std::string::size_type key_start, key_end, val_start, val_end;
        key_start = key_end = val_start = val_end = 0;

        for (std::string::size_type i = 0; i < input.size(); ++i)
        {
            if (input[i] == '"' and (i == 0 or input[i-1] != '\\'))
            {
                in_quotes = not in_quotes;
                if (in_quotes)
                {
                    if (key_start == 0)
                    {
                        key_start = i+1;
                    }
                    else if (val_start == 0)
                    {
                        val_start = i+1;
                    }
                }
                else
                {
                    if (key_end == 0)
                    {
                        key_end = i-1;
                    }
                    else if (val_end == 0)
                    {
                        val_end = i-1;
                        result[input.substr(key_start, key_end-key_start+1)] = unescape_hstore_text(input.substr(val_start, val_end-val_start+1));
                        key_start = key_end = val_start = val_end = 0;
                    }
                }
            }
            else if ((not in_quotes) && input[i] == '=' && input[i+1] == '>')
            {
                ++i;
            }
        }

        return result;
    }
}

#endif // LIBPQ_RAII_HPP
