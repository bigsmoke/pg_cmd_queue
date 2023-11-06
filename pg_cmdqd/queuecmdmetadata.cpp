#include "queuecmdmetadata.h"

#include <chrono>

#include "pq-raii/libpq-raii.hpp"
#include "utils.h"

QueueCmdMetadata::QueueCmdMetadata(
        std::shared_ptr<PG::result> &result,
        int row_number,
        const std::unordered_map<std::string, int> &field_numbers
    ) noexcept
{
    try
    {
        queue_cmd_class = PQ::getvalue(result, row_number, field_numbers.at("queue_cmd_class"));

        queue_cmd_relname = PQ::getvalue(result, row_number, field_numbers.at("queue_cmd_relname"));

        cmd_id = PQ::getvalue(result, row_number, field_numbers.at("cmd_id"));

        cmd_subid = PQ::getnullable(result, row_number, field_numbers.at("cmd_subid"));

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
    ).count() / 1000000.0;
}

void QueueCmdMetadata::stamp_start_time()
{
    this->cmd_runtime_start = unix_timestamp();
}

void QueueCmdMetadata::stamp_end_time()
{
    this->cmd_runtime_end = unix_timestamp();
}
