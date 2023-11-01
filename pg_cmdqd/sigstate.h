#ifndef SIGSTATE_HPP
#define SIGSTATE_HPP

#include <initializer_list>

inline bool sig_recv[32];

/**
 * Returns the first of the signal numbers given as arguments that has been received according
 * to the `sig_recv` array, or zero if none of these signals were received.
 */
int sig_num_received(std::initializer_list<int> sig_nums);

#endif // SIGSTATE_HPP
