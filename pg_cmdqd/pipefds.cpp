#include "pipefds.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <stdexcept>

PipeFds::PipeFds(int pipe2_flags)
{
    if (pipe(this->fds) == -1)
        throw std::runtime_error(strerror(errno));

    if (pipe2_flags != 0) {
        if ((fcntl(this->fds[0], F_SETFL, fcntl(this->fds[0], F_GETFL) | O_NONBLOCK)) < 0)
            throw std::runtime_error(strerror(errno));
        if ((fcntl(this->fds[1], F_SETFL, fcntl(this->fds[1], F_GETFL) | O_NONBLOCK)) < 0)
            throw std::runtime_error(strerror(errno));
    }
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
    if (this->fds[0] == -1) return;

    int close_result;
    while ((close_result = close(fds[0])) == -1 && errno == EINTR) {}
    this->fds[0] = -1;
    if (close_result == -1) throw std::runtime_error(strerror(errno));
}

void PipeFds::close_write_fd()
{
    if (this->fds[1] == -1) return;

    int close_result;
    while ((close_result = close(fds[1])) == -1 && errno == EINTR) {}
    this->fds[1] = -1;
    if (close_result == -1) throw std::runtime_error(strerror(errno));
}
