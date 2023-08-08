#include "lwpg_nullable.h"

lwpg::nullable_string
lwpg::getnullable(const PGresult *res, int row_number, int column_number)
{
    if (PQgetisnull(res, row_number, column_number))
    {
        return {};
    }

    return PQgetvalue(res, row_number, column_number);
}
