#include "lwpg_nullable.h"

#include <optional>
#include <string>

std::optional<std::string> lwpg::getnullable(const PGresult *res, int row_number, int column_number)
{
    if (PQgetisnull(res, row_number, column_number))
    {
        return {};
    }

    return PQgetvalue(res, row_number, column_number);
}

std::optional<std::string> lwpg::to_nullable_string(const std::optional<int>& orig)
{
    if (not orig.has_value())
        return {};

    return std::to_string(orig.value());
}
