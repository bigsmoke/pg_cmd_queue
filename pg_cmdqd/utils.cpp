#include "utils.h"

#include <cstring>
#include <cstdarg>

#include <signal.h>

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
