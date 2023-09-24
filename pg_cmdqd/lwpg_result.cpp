#include "lwpg_result.h"


lwpg::Result::Result(PGresult *res) :
    result(res)
{

}

lwpg::Result::~Result()
{
    if (!this->result)
        return;

    PQclear(this->result);
}

ExecStatusType lwpg::Result::getResultStatus() const
{
    return PQresultStatus(result);
}

PGresult *lwpg::Result::get()
{
    return this->result;
}

