#ifndef LWPG_NULLABLE_H
#define LWPG_NULLABLE_H

#include <string>
#include <variant>

#include "optional.hpp"  // With C++17, `tl::optional` can be replaced with `std::optional`.

#include <postgresql/libpq-fe.h>

namespace lwpg
{
    typedef tl::optional<std::string> nullable_string;

    nullable_string getnullable(const PGresult *res, int row_number, int column_number);
}

#endif // LWPG_NULLABLE_H
