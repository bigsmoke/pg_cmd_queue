#ifndef PIPEFDS_H
#define PIPEFDS_H

class PipeFds
{
    int fds[2];

public:
    PipeFds();
    ~PipeFds();
    int* data();
    int read_fd() const;
    int write_fd() const;
    void close_read_fd();
    void close_write_fd();
};

#endif // PIPEFDS_H
