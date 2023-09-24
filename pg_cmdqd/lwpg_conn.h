#ifndef LWPG_CONN_H
#define LWPG_CONN_H

#include <string>
#include <stdexcept>
#include <memory>

#include <libpq-fe.h>

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

    inline std::shared_ptr<Conn> connectdb(const std::string &conninfo)
    {
        return std::make_shared<lwpg::Conn>(PQconnectdb(conninfo.c_str()));
    }
}

#endif // LWPG_CONN_H
