#include "queuecmdmetadata.h"

#include <chrono>

#include "pq-raii/libpq-raii.hpp"


QueueCmdMetadata::QueueCmdMetadata(
        const PG::result &result,
        int row_number,
        const std::unordered_map<std::string, int> &field_numbers
    ) noexcept
{
    try
    {
        cmd_class_identity = PQ::getvalue(result, row_number, field_numbers.at("cmd_class_identity"));

        cmd_class_relname = PQ::getvalue(result, row_number, field_numbers.at("cmd_class_relname"));

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

QueueCmdMetadata::QueueCmdMetadata(
        const std::string &cmd_class_identity,
        const std::string &cmd_class_relname,
        const std::string &cmd_id,
        const std::optional<std::string> &cmd_subid
    )
    : cmd_class_identity(cmd_class_identity),
      cmd_class_relname(cmd_class_relname),
      cmd_id(cmd_id),
      cmd_subid(cmd_subid)
{
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
