#ifndef LWPG_RESULTS_H
#define LWPG_RESULTS_H

#include "lwpg_result.h"
#include "lwpg_result_iterator.h"

/**
 * @brief The LWPGresults struct
 *
 * This class is also the RAII place to deal with finishing async operations. Async operations need to continue until there's nothing more to read,
 * otherwise the connection can't be used for something else. The destructor would be the best place. It would need a shared pointer to
 * a LWPGresult then.
 */
template<typename T>
class LWPGresults
{
    LWPGresult_iterator<T> _begin;
    int rowCount = 0;

public:
    LWPGresults<T>(LWPGresult_iterator<T> &begin, int rowCount);

    LWPGresult_iterator<T> begin();
    LWPGresult_iterator<T> end();
};

template<typename T>
LWPGresults<T>::LWPGresults(LWPGresult_iterator<T> &begin, int rowCount) :
    _begin(begin),
    rowCount(rowCount)
{

}

template<typename T>
LWPGresult_iterator<T> LWPGresults<T>::begin()
{
    return _begin;
}

template<typename T>
LWPGresult_iterator<T> LWPGresults<T>::end()
{
    LWPGresult_iterator<T> end(this->rowCount);
    return end;
}

#endif // LWPG_RESULTS_H
