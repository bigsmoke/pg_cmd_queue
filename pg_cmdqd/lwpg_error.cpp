#include "lwpg_error.h"

#include "libpq-fe.h"
#include "postgres_ext.h"

#include "lwpg_array.h"
#include "lwpg_nullable.h"
#include "lwpg_string.h"

lwpg::Error::Error(std::shared_ptr<lwpg::Result> &result)
    : Error(std::shared_ptr<lwpg::Result>(result)->get())
{
}

lwpg::Error::Error(const PGresult *res)
{
    pg_diag_severity              = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_SEVERITY));
    pg_diag_severity_nonlocalized = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_SEVERITY_NONLOCALIZED));
    pg_diag_sqlstate              = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_SQLSTATE));
    pg_diag_message_primary       = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
    pg_diag_message_detail        = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_MESSAGE_DETAIL));
    pg_diag_message_hint          = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_MESSAGE_HINT));
    pg_diag_statement_position    = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_STATEMENT_POSITION));
    pg_diag_internal_position     = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_INTERNAL_POSITION));
    pg_diag_internal_query        = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_INTERNAL_QUERY));
    pg_diag_context               = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_CONTEXT));
    pg_diag_schema_name           = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_SCHEMA_NAME));
    pg_diag_table_name            = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_TABLE_NAME));
    pg_diag_column_name           = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_COLUMN_NAME));
    pg_diag_datatype_name         = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_DATATYPE_NAME));
    pg_diag_constraint_name       = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_CONSTRAINT_NAME));
    pg_diag_source_file           = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_SOURCE_FILE));
    pg_diag_source_line           = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_SOURCE_LINE));
    pg_diag_source_function       = lwpg::to_nullable_string(PQresultErrorField(res, PG_DIAG_SOURCE_FUNCTION));
}

std::string lwpg::to_string(const Error &err)
{
    return lwpg::composite_value({
        err.pg_diag_severity,
        err.pg_diag_severity_nonlocalized,
        err.pg_diag_sqlstate,
        err.pg_diag_message_primary,
        err.pg_diag_message_detail,
        err.pg_diag_message_hint,
        err.pg_diag_statement_position,
        err.pg_diag_internal_position,
        err.pg_diag_internal_query,
        err.pg_diag_context,
        err.pg_diag_schema_name,
        err.pg_diag_table_name,
        err.pg_diag_column_name,
        err.pg_diag_datatype_name,
        err.pg_diag_constraint_name,
        err.pg_diag_source_file,
        err.pg_diag_source_line,
        err.pg_diag_source_function,
    });
}

std::optional<std::string> lwpg::to_nullable_string(const std::optional<lwpg::Error> &orig)
{
    if (not orig.has_value())
        return {};

    return lwpg::to_string(orig.value());
}

std::string lwpg::to_string(const std::vector<Error> &errs)
{
    std::vector<std::string> string_list;
    string_list.reserve(errs.size());

    for (const Error &err : errs) string_list.push_back(lwpg::to_string(err));

    return lwpg::to_string(string_list);
}
