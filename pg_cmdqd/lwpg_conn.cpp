#include "lwpg_conn.h"

#include "stdexcept"

#include "iostream"
#include "stdio.h"

#include "lwpg_result.h"

lwpg::Conn::Conn(PGconn *conn) :
    conn(conn)
{

}

lwpg::Conn::~Conn()
{
    if (!conn)
        return;

    PQfinish(conn);
}

PGconn *lwpg::Conn::get()
{
    return this->conn;
}
