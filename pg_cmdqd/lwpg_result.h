#ifndef LWPG_RESULT_H
#define LWPG_RESULT_H

#include <memory>

#include <postgresql/libpq-fe.h>

#include "lwpg_conn.h"

namespace lwpg
{

    /**
     * @brief The `result` struct is basically just a RAII wrapper for `PGresult`.
     */
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

}

#endif // LWPG_RESULT_H
