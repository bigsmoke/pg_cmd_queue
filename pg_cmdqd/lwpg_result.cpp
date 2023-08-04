#include "lwpg_result.h"


LWPGresult::LWPGresult(PGresult *res) :
    result(res)
{

}

LWPGresult::~LWPGresult()
{
    if (!this->result)
        return;

    PQclear(this->result);
}

ExecStatusType LWPGresult::getResultStatus() const
{
    return PQresultStatus(result);
}

PGresult *LWPGresult::get()
{
    return this->result;
}
