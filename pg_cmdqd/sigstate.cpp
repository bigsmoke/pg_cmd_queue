#include "sigstate.h"

int sig_num_received(std::initializer_list<int> sig_nums)
{
    for (const int sig_num: sig_nums)
        if (sig_recv[sig_num])
            return sig_num;

    return 0;
}
