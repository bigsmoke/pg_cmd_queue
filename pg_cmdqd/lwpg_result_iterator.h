#ifndef LWPG_RESULT_ITERATOR_H
#define LWPG_RESULT_ITERATOR_H

#include <memory>
#include <unordered_map>

#include "lwpg_result.h"

namespace lwpg
{

    /**
     * \brief The ResultIterator knows how to iterate over rows and use operatior*() to construct an object of the templated type.
     *
     * It's important that your type does not throw exceptions in its constructor. Also, the operator*() doesn't return a reference, because
     * that's impossible, to the objected created is a local copy, which can be moved.
     */
    template<typename T>
    class ResultIterator
    {
        std::shared_ptr<lwpg::Result> result;
        std::shared_ptr<lwpg::Conn> conn;
        std::unordered_map<std::string, int> fieldMappings;
        int row = 0;
        int rowCount = 0;
        int fieldCount = 0;

    public:

        ResultIterator() = delete;
        ResultIterator(int rowCount);
        ResultIterator(std::shared_ptr<lwpg::Result> &result, std::shared_ptr<lwpg::Conn> &conn, int rowCount, int fieldCount);

        bool operator!=(ResultIterator &rhs);
        ResultIterator &operator++(int);
        ResultIterator &operator++();
        T operator*();
    };

    /**
     * \brief basically the constructor for the end iterator.
     */
    template<typename T>
    ResultIterator<T>::ResultIterator(int rowCount) :
        row(rowCount)
    {

    }

    template<typename T>
    ResultIterator<T>::ResultIterator(std::shared_ptr<lwpg::Result> &result, std::shared_ptr<lwpg::Conn> &conn, int rowCount, int fieldCount) :
        result(result),
        conn(conn),
        rowCount(rowCount),
        fieldCount(fieldCount)
    {
        for (int i = 0; i < fieldCount; i++)
        {
            std::string value = PQfname(result->get(), i);
            fieldMappings[value] = i;
        }
    }

    template<typename T>
    bool ResultIterator<T>::operator!=(ResultIterator<T> &rhs)
    {
        return this->row != rhs.row;
    }

    template<typename T>
    ResultIterator<T> &ResultIterator<T>::operator++(int)
    {
        row++;
        return *this;
    }

    template<typename T>
    ResultIterator<T> &ResultIterator<T>::operator++()
    {
        return operator++(0);
    }

    template<typename T>
    T ResultIterator<T>::operator*()
    {
        if (row >= rowCount)
            std::runtime_error("Trying to dereference invalid iterator");

        T t(result, row, fieldMappings);
        return t;
    }

}

#endif // LWPG_RESULT_ITERATOR_H
