#pragma once

#include "common/net/message_id.h"
#include "framework/protocol/handler_context.h"

#include <optional>
#include <string>

namespace framework::execution {

enum class ExecutionKeyKind {
    kDirect,
    kConnection,
    kSession,
    kPlayer,
};

struct ExecutionKey {
    ExecutionKeyKind kind = ExecutionKeyKind::kDirect;
    std::string value;
};

struct RequestExecutionPolicy {
    ExecutionKeyKind key_kind = ExecutionKeyKind::kDirect;
};

std::optional<ExecutionKey> BuildExecutionKey(const RequestExecutionPolicy& policy,
                                              const framework::protocol::HandlerContext& context);
std::string DescribeExecutionKey(const ExecutionKey& key);

}  // namespace framework::execution
