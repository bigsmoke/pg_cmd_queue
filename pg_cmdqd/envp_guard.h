#ifndef ENVP_GUARD_H
#define ENVP_GUARD_H

#include <string>
#include <unordered_map>

/**
 * RAII wrapper around a `execv()` `envp` argument compatible list of command arguments.
 *
 * TODO: Review with Wiebe.
 */
class envp_guard
{
    char **c_envp;
    int argc = -1;

public:
    envp_guard() = delete;
    envp_guard(const std::unordered_map<std::string, std::string> &env);
    ~envp_guard();

    char * const * get();

    /**
     * Returns a pointer to an array of `char` arrays pointer.
     *
     * Each pointer in the returned array
     */
    explicit operator char *const *();
};

#endif // ENVP_GUARD_H
