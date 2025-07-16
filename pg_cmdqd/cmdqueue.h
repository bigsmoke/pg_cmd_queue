#ifndef CMDQUEUE_H
#define CMDQUEUE_H

#include <memory>
#include <string>
#include <unordered_map>

#include "pq-raii/libpq-raii.hpp"

class CmdQueue
{
    bool _is_valid = false;
    std::string _validation_error_message;

public:
    static const std::string CMD_QUEUE_RELNAME;
    static const std::string SELECT_STMT;

    /**
     * The schema-qualified name of the relationship holding the queue, quoted where necessary.
     *
     * The term “identity” has been chosen, because `identity` is in fact also
     * the name of the field returned by the `pg_identify_object()` function
     * that we use.
     */
    std::string cmd_class_identity;

    /**
     * The unquoted schema-local name of the relationship with the queue commands.
     *
     * The suffix is `_relname` was chosen for consistency with Postgres' `pg_class` catalog.
     */
    std::string cmd_class_relname;

    /**
     * The local part of the queue command template table name.
     */
    std::string cmd_signature_class_relname;

    std::optional<std::string> queue_runner_role;
    std::optional<std::string> queue_notify_channel;
    int queue_reselect_interval_msec;
    std::optional<int> queue_reselect_randomized_every_nth;
    double queue_cmd_timeout_sec;
    std::string ansi_fg;

    CmdQueue() = default;
    CmdQueue(const PG::result &result, int row_number, const std::unordered_map<std::string, int> &field_numbers) noexcept;
    bool is_valid() const;
    std::string validation_error_message() const;
};

#endif  // CMDQUEUE_H
