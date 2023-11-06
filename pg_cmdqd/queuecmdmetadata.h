#ifndef QUEUECMDMETADATA_H
#define QUEUECMDMETADATA_H

#include <optional>
#include <string>
#include <unordered_map>

#include "pq-raii/libpq-raii.hpp"
#include "cmdqueue.h"
#include "logger.h"

class QueueCmdMetadata
{
protected:
    Logger *logger = Logger::getInstance();

    bool _is_valid = false;

public:
    std::string queue_cmd_class;
    std::string queue_cmd_relname;
    std::string cmd_id;
    std::optional<std::string> cmd_subid;

    // PostgreSQL has a `to_timestamp(double) function which expects the subsecond digits as the decimal part.
    double cmd_queued_since;
    double cmd_runtime_start;
    double cmd_runtime_end;

    QueueCmdMetadata() = delete;
    QueueCmdMetadata(
            std::shared_ptr<PG::result> &result,
            int row_number,
            const std::unordered_map<std::string, int> &fieldMapping
        ) noexcept;
    QueueCmdMetadata(
        const std::string &queue_cmd_class,
        const std::string &cmd_id,
        const std::optional<std::string> &cmd_subid
    );

    ~QueueCmdMetadata() = default;

    bool is_valid() const;

    static double unix_timestamp();
    void stamp_start_time();
    void stamp_end_time();
};

#endif // QUEUECMDMETADATA_H
