#include "fdguard.h"

#include <string.h>
#include <unistd.h>

#include <stdexcept>

FdGuard::FdGuard(const int fd)
    : _fd(fd)
{
    if (_fd == -1)
        throw std::runtime_error(strerror(errno));
}

FdGuard::~FdGuard()
{
    int close_result;
    while ((close_result = close(_fd)) == -1 && errno == EINTR) {}
}

int FdGuard::fd() const
{
    return _fd;
}
