#ifndef LWPG_RESULTS_H
#define LWPG_RESULTS_H

#include "lwpg_result.h"
#include "lwpg_result_iterator.h"

namespace lwpg
{

    /**
     * @brief The Results struct
     *
     * This class is also the RAII place to deal with finishing async operations. Async operations need to continue until there's nothing more to read,
     * otherwise the connection can't be used for something else. The destructor would be the best place. It would need a shared pointer to
     * a Result then.
     */
    template<typename T>
    class Results
    {
        lwpg::ResultIterator<T> _begin;
        int rowCount = 0;

    public:
        Results<T>(ResultIterator<T> &begin, int rowCount);

        ResultIterator<T> begin();
        ResultIterator<T> end();
    };

    template<typename T>
    Results<T>::Results(lwpg::ResultIterator<T> &begin, int rowCount) :
        _begin(begin),
        rowCount(rowCount)
    {

    }

    template<typename T>
    ResultIterator<T> Results<T>::begin()
    {
        return _begin;
    }

    template<typename T>
    ResultIterator<T> Results<T>::end()
    {
        ResultIterator<T> end(this->rowCount);
        return end;
    }
}

#endif // LWPG_RESULTS_H
