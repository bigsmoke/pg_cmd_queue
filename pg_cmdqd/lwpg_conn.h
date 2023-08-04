#ifndef LWPG_CONN_H
#define LWPG_CONN_H

#include <string>
#include <stdexcept>
#include <memory>

#include "postgresql/libpq-fe.h"

/**
 * @brief Just a light-weight RAII wrapper for PGconn.
 */
class LWPGconn
{
    PGconn *conn = nullptr;

public:
    LWPGconn(const LWPGconn &other) = delete;
    LWPGconn(PGconn *conn);
    ~LWPGconn();

    PGconn *get();
};



#endif // LWPG_CONN_H
