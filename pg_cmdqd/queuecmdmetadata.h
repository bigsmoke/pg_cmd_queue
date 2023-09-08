#ifndef QUEUECMDMETADATA_H
#define QUEUECMDMETADATA_H

#include <optional>
#include <string>
#include <unordered_map>

#include "cmdqueue.h"
#include "logger.h"
#include "lwpg_context.h"

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
    QueueCmdMetadata(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept;
    ~QueueCmdMetadata() = default;

    bool is_valid() const;

    static double unix_timestamp();
    void stamp_start_time();
    void stamp_end_time();
};

#endif // QUEUECMDMETADATA_H
