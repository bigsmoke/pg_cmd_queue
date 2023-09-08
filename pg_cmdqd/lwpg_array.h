#ifndef LWPG_ARRAY_H
#define LWPG_ARRAY_H

#include <string>
#include <vector>

namespace lwpg
{
    /**
     * Convert a PostgreSQL array string to a vector of strings representing the array member values.
     */
    std::vector<std::string> array_to_vector(const std::string &input);

    std::string to_string(const std::vector<std::string> &array);
}

#endif // LWPG_ARRAY_H
