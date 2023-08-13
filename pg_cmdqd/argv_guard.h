#ifndef ARGV_GUARD_H
#define ARGV_GUARD_H

#include <string>
#include <vector>

/**
 * RAII wrapper around a `execv()` `argv` argument compatible list of command arguments.
 *
 * TODO: Review with Wiebe.
 */
class argv_guard
{
    char **c_argv;
    int argc = -1;

public:
    argv_guard() = delete;
    argv_guard(const std::vector<std::string> &argv);
    ~argv_guard();

    char * const * get();

    /**
     * Returns a pointer to an array of `char` arrays pointer.
     *
     * Each pointer in the returned array
     */
    explicit operator char *const *();
};

#endif // ARGV_GUARD_H
