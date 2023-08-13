#include "pipefds.h"

#include <string.h>
#include <unistd.h>

#include <stdexcept>

PipeFds::PipeFds()
{
    if (pipe(this->fds) == -1)
    {
        throw std::runtime_error(strerror(errno));
    }
}

PipeFds::~PipeFds()
{
    close_for_reading();
    close_for_writing();
}
