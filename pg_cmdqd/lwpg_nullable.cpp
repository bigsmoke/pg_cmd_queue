#include "lwpg_nullable.h"

#include <string>

lwpg::nullable_string lwpg::getnullable(const PGresult *res, int row_number, int column_number)
{
    if (PQgetisnull(res, row_number, column_number))
    {
        return {};
    }

    return PQgetvalue(res, row_number, column_number);
}

lwpg::nullable_string lwpg::to_nullable_string(const lwpg::nullable_int& orig)
{
    if (not orig.has_value())
        return {};

    return std::to_string(orig.value());
}
