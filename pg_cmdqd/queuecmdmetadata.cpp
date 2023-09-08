#include "queuecmdmetadata.h"

#include <chrono>

#include "lwpg_result.h"
#include "utils.h"

QueueCmdMetadata::QueueCmdMetadata(std::shared_ptr<lwpg::Result> &result, int row, const std::unordered_map<std::string, int> &fieldMapping) noexcept
{
    try
    {
        queue_cmd_class = PQgetvalue(result->get(), row, fieldMapping.at("queue_cmd_class"));

        queue_cmd_relname = PQgetvalue(result->get(), row, fieldMapping.at("queue_cmd_relname"));

        cmd_id = PQgetvalue(result->get(), row, fieldMapping.at("cmd_id"));

        cmd_subid = lwpg::getnullable(result->get(), row, fieldMapping.at("cmd_subid"));

        _is_valid = true;
    }
    catch (std::exception &ex)
    {
        logger->log(LOG_ERROR, "Error loading queue command meta data: %s", ex.what());
        _is_valid = false;
    }
}

bool QueueCmdMetadata::is_valid() const
{
    return _is_valid;
}

double QueueCmdMetadata::unix_timestamp()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() / 1000000;
}

void QueueCmdMetadata::stamp_start_time()
{
    this->cmd_runtime_start = unix_timestamp();
}

void QueueCmdMetadata::stamp_end_time()
{
    this->cmd_runtime_end = unix_timestamp();
}
