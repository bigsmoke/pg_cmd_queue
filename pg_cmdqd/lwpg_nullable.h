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

    typedef tl::optional<int> nullable_int;

    nullable_string to_nullable_string(const nullable_int& orig);
}

#endif // LWPG_NULLABLE_H
