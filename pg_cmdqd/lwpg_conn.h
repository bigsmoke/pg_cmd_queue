#ifndef LWPG_CONN_H
#define LWPG_CONN_H

#include <string>
#include <stdexcept>
#include <memory>

#include "postgresql/libpq-fe.h"

namespace lwpg
{

    /**
     * @brief Just a light-weight RAII wrapper for PGconn.
     */
    class Conn
    {
        PGconn *conn = nullptr;

    public:
        Conn(const Conn &other) = delete;
        Conn(PGconn *conn);
        ~Conn();

        PGconn *get();
    };

}

#endif // LWPG_CONN_H
