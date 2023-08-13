#ifndef PIPEFDS_H
#define PIPEFDS_H

class PipeFds
{
    int fds[2];

public:
    PipeFds();
    ~PipeFds();
    void close_for_reading();
    void close_for_writing();
};

#endif // PIPEFDS_H
