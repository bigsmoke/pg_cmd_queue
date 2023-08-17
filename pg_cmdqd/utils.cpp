#include "utils.h"

#include <cstring>
#include <cstdarg>

#include <signal.h>

std::string formatString(const std::string str, ...)
{
    char buf[512];

    va_list valist;
    va_start(valist, str);
    vsnprintf(buf, 512, str.c_str(), valist);
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
