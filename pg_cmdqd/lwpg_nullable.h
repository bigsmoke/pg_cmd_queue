#ifndef LWPG_NULLABLE_H
#define LWPG_NULLABLE_H

#include <optional>
#include <string>
#include <variant>

#include <postgresql/libpq-fe.h>

namespace lwpg
{

    std::optional<std::string> getnullable(const PGresult *res, int row_number, int column_number);

    std::optional<std::string> to_nullable_string(const std::optional<int>& orig);
}

#endif // LWPG_NULLABLE_H
