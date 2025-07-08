#ifndef UTILS_H
#define UTILS_H

#include <string.h>
#include <errno.h>

#include <string>
#include <unordered_map>

template<typename T> int check(int rc)
{
    if (rc < 0)
    {
        char *err = strerror(errno);
        std::string msg(err);
        throw T(msg);
    }

    return rc;
}

std::string formatString(const std::string str, ...);

int maskAllSignalsCurrentThread();

std::unordered_map<std::string, std::string> environ_to_unordered_map(char **environ);

template<typename K, typename V> std::unordered_map<K, V>
inline throw_if_missing_any_value(const std::unordered_map<K, std::optional<V>> &map)
{
    // TODO: This is super ugly. Maybe Wiebe has ideas to only do the checking and make
    //       this non-optional without copying everything.
    std::unordered_map<std::string, std::string> nonoptional_map;
    for (auto pair : map)
    {
        if (not pair.second)
            throw std::runtime_error("Unexpected key without value.");
        nonoptional_map[pair.first] = pair.second.value();
    }
    return nonoptional_map;
}

#endif // UTILS_H
