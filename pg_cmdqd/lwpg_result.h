#ifndef LWPG_RESULT_H
#define LWPG_RESULT_H

#include <memory>

#include <postgresql/libpq-fe.h>

#include "lwpg_conn.h"


/**
 * @brief The LWPGresult struct is basically just a RAII wrapper for PGresult.
 */
class LWPGresult
{
    PGresult *result = nullptr;

public:
    LWPGresult(const LWPGresult &other) = delete;
    LWPGresult(PGresult *res);
    ~LWPGresult();

    ExecStatusType getResultStatus() const;
    PGresult *get();
};



#endif // LWPG_RESULT_H
