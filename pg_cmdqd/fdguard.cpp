#include "fdguard.h"

#include <errno.h>
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
    while (close(_fd) == -1 && errno == EINTR) {}
}

int FdGuard::fd() const
{
    return _fd;
}
