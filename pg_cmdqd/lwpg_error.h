#ifndef SQLQUEUECMDERROR_H
#define SQLQUEUECMDERROR_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lwpg_result.h"

namespace lwpg
{

class Error
{
public:
    std::optional<std::string> pg_diag_severity;
    std::optional<std::string> pg_diag_severity_nonlocalized;
    std::optional<std::string> pg_diag_sqlstate;
    std::optional<std::string> pg_diag_message_primary;
    std::optional<std::string> pg_diag_message_detail;
    std::optional<std::string> pg_diag_message_hint;
    std::optional<std::string> pg_diag_statement_position;
    std::optional<std::string> pg_diag_internal_position;
    std::optional<std::string> pg_diag_internal_query;
    std::optional<std::string> pg_diag_context;
    std::optional<std::string> pg_diag_schema_name;
    std::optional<std::string> pg_diag_table_name;
    std::optional<std::string> pg_diag_column_name;
    std::optional<std::string> pg_diag_datatype_name;
    std::optional<std::string> pg_diag_constraint_name;
    std::optional<std::string> pg_diag_source_file;
    std::optional<std::string> pg_diag_source_line;
    std::optional<std::string> pg_diag_source_function;

    Error() = default;
    Error(std::shared_ptr<lwpg::Result> &result);
    Error(const PGresult *res);
};

std::string to_string(const Error &err);
std::string to_string(const std::vector<Error> &errs);
std::optional<std::string> to_nullable_string(const std::optional<lwpg::Error> &orig);

}

# endif // SQLQUEUECMDERROR_H
