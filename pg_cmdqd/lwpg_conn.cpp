#include "lwpg_conn.h"

#include "stdexcept"

#include "iostream"
#include "stdio.h"

#include "lwpg_result.h"

LWPGconn::LWPGconn(PGconn *conn) :
    conn(conn)
{

}

LWPGconn::~LWPGconn()
{
    if (!conn)
        return;

    PQfinish(conn);
}

PGconn *LWPGconn::get()
{
    return this->conn;
}
