#include "pipefds.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <stdexcept>

PipeFds::PipeFds()
{
    if (pipe(this->fds) == -1)
        throw std::runtime_error(strerror(errno));
}

PipeFds::~PipeFds()
{
    close_read_fd();
    close_write_fd();
}

int* PipeFds::data()
{
    return fds;
}

int PipeFds::read_fd() const
{
    return this->fds[0];
}

int PipeFds::write_fd() const
{
    return this->fds[1];
}

void PipeFds::close_read_fd()
{
    int close_result;
    while ((close_result = close(fds[0])) == -1 && errno == EINTR) {}
    if (close_result == -1) throw std::runtime_error(strerror(errno));
}

void PipeFds::close_write_fd()
{
    int close_result;
    while ((close_result = close(fds[1])) == -1 && errno == EINTR) {}
    if (close_result == -1) throw std::runtime_error(strerror(errno));
}
