#ifndef LWPG_RESULT_ITERATOR_H
#define LWPG_RESULT_ITERATOR_H

#include <memory>
#include <unordered_map>

#include "lwpg_result.h"

/**
 * \brief The LWPGresult_iterator knows how to iterate over rows and use operatior*() to construct an object of the templated type.
 *
 * It's important that your type does not throw exceptions in its constructor. Also, the operator*() doesn't return a reference, because
 * that's impossible, to the objected created is a local copy, which can be moved.
 */
template<typename T>
class LWPGresult_iterator
{
    std::shared_ptr<LWPGresult> result;
    std::shared_ptr<LWPGconn> conn;
    std::unordered_map<std::string, int> fieldMappings;
    int row = 0;
    int rowCount = 0;
    int fieldCount = 0;

public:

    LWPGresult_iterator() = delete;
    LWPGresult_iterator(int rowCount);
    LWPGresult_iterator(std::shared_ptr<LWPGresult> &result, std::shared_ptr<LWPGconn> &conn, int rowCount, int fieldCount);

    bool operator!=(LWPGresult_iterator &rhs);
    LWPGresult_iterator &operator++(int);
    LWPGresult_iterator &operator++();
    T operator*();
};

/**
 * \brief basically the constructor for the end iterator.
 */
template<typename T>
LWPGresult_iterator<T>::LWPGresult_iterator(int rowCount) :
    row(rowCount)
{

}

template<typename T>
LWPGresult_iterator<T>::LWPGresult_iterator(std::shared_ptr<LWPGresult> &result, std::shared_ptr<LWPGconn> &conn, int rowCount, int fieldCount) :
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
bool LWPGresult_iterator<T>::operator!=(LWPGresult_iterator<T> &rhs)
{
    return this->row != rhs.row;
}

template<typename T>
LWPGresult_iterator<T> &LWPGresult_iterator<T>::operator++(int)
{
    row++;
    return *this;
}

template<typename T>
LWPGresult_iterator<T> &LWPGresult_iterator<T>::operator++()
{
    return operator++(0);
}

template<typename T>
T LWPGresult_iterator<T>::operator*()
{
    if (row >= rowCount)
        std::runtime_error("Trying to dereference invalid iterator");

    T t(result, row, fieldMappings);
    return t;
}

#endif // LWPG_RESULT_ITERATOR_H
