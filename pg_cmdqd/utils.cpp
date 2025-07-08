#include "utils.h"

#include <cstring>
#include <cstdarg>
#include <stdexcept>
#include <iostream>

#include <signal.h>
#include <string.h>

std::string formatString(const std::string str, ...)
{
    va_list valist;

    va_start(valist, str);
    int buf_size = vsnprintf(nullptr, 0, str.c_str(), valist) + 1;
    char buf[buf_size];

    va_start(valist, str);
    vsnprintf(buf, buf_size, str.c_str(), valist);
    va_end(valist);

    size_t len = strlen(buf);
    std::string result(buf, len);

    return result;
}

int maskAllSignalsCurrentThread()
{
    sigset_t set;
    sigfillset(&set);

    int r = pthread_sigmask(SIG_SETMASK, &set, NULL);
    return r;
}

std::unordered_map<std::string, std::string> environ_to_unordered_map(char **environ)
{
    std::unordered_map<std::string, std::string> map;

    for (int i = 0; environ[i]; i++)
    {
        const char* equalSign = strchr(environ[i], '=');
        if (equalSign == nullptr)
            throw std::runtime_error("`**environ` member strings are each expected to have an equals sign.");
        map[std::string(environ[i], equalSign - *&environ[i])] = std::string(equalSign+1);
    }

    return map;
}
