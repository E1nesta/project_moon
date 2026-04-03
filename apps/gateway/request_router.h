#pragma once

#include "runtime/execution/execution_types.h"
#include "runtime/protocol/message_id.h"

#include <optional>
#include <string>

namespace framework::protocol {
struct HandlerContext;
}

namespace services::gateway {

class GatewayForwardExecutor;

class RequestRouter {
public:
    [[nodiscard]] std::optional<framework::execution::ExecutionKey> BuildExecutionKey(
        common::net::MessageId message_id,
        const framework::protocol::HandlerContext& context) const;

    [[nodiscard]] std::string DescribeDispatchTarget(common::net::MessageId message_id,
                                                     const framework::protocol::HandlerContext& context,
                                                     const GatewayForwardExecutor* forward_executor) const;
};

}  // namespace services::gateway
