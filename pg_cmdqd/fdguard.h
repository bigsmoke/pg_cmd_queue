#ifndef FDGUARD_H
#define FDGUARD_H

/**
 * RAII wrapper for a *nix FD.
 */
class FdGuard
{
    int _fd;

public:
    /**
     * The constructor will throw a `std::runtime_error` when `fd == -1`.
     */
    FdGuard(const int fd);
    ~FdGuard();
    int fd() const;
};

#endif // FDGUARD_H
