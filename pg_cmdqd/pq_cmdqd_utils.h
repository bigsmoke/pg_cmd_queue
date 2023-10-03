#ifndef PQ_UTILS_H
#define PQ_UTILS_H

#include "pq-raii/libpq-raii.hpp"
#include "logger.h"

void maintain_connection(const std::string &conn_str, std::shared_ptr<PG::conn> &conn);

#endif // PQ_UTILS_H
