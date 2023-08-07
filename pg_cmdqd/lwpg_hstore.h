#ifndef LWPG_HSTORE_H
#define LWPG_HSTORE_H

#include <string>
#include <unordered_map>

namespace lwpg
{
    /**
     * Parse Postgres `hstore` string to an `unordered_map` of each item in the `hstore`.
     */
    std::unordered_map<std::string, std::string> hstore_to_unordered_map(std::string);
}

#endif // LWPG_HSTORE_H
