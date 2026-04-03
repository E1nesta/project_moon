#include "apps/gateway/request_router.h"

#include "apps/gateway/forward_executor.h"
#include "runtime/protocol/handler_context.h"

namespace services::gateway {

std::optional<framework::execution::ExecutionKey> RequestRouter::BuildExecutionKey(
    common::net::MessageId message_id,
    const framework::protocol::HandlerContext& context) const {
    if (message_id == common::net::MessageId::kLoginRequest) {
        return framework::execution::ExecutionKey{
            framework::execution::ExecutionKeyKind::kConnection, std::to_string(context.connection_id)};
    }

    if (context.request.player_id != 0) {
        return framework::execution::ExecutionKey{
            framework::execution::ExecutionKeyKind::kPlayer, std::to_string(context.request.player_id)};
    }

    if (!context.request.auth_token.empty()) {
        return framework::execution::ExecutionKey{
            framework::execution::ExecutionKeyKind::kSession, context.request.auth_token};
    }

    return framework::execution::ExecutionKey{
        framework::execution::ExecutionKeyKind::kConnection, std::to_string(context.connection_id)};
}

std::string RequestRouter::DescribeDispatchTarget(common::net::MessageId message_id,
                                                  const framework::protocol::HandlerContext& context,
                                                  const GatewayForwardExecutor* forward_executor) const {
    if (message_id == common::net::MessageId::kPingRequest) {
        return "direct";
    }

    const auto execution_key = BuildExecutionKey(message_id, context);
    if (!execution_key.has_value() || forward_executor == nullptr) {
        return "forward-unresolved";
    }

    const auto shard = forward_executor->PreviewShard(*execution_key);
    if (!shard.has_value()) {
        return "forward-unavailable";
    }
    return "forward-shard-" + std::to_string(*shard);
}

}  // namespace services::gateway
