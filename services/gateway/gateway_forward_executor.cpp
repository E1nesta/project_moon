#include "services/gateway/gateway_forward_executor.h"

#include <utility>

namespace services::gateway {

GatewayForwardExecutor::GatewayForwardExecutor(Options options)
    : options_(std::move(options)),
      executor_({options_.worker_threads, options_.worker_threads, options_.queue_limit}),
      login_upstream_(std::make_unique<framework::transport::UpstreamClientPool>(
          options_.login.host, options_.login.port, options_.login.timeout_ms, options_.login.pool_size)),
      player_upstream_(std::make_unique<framework::transport::UpstreamClientPool>(
          options_.player.host, options_.player.port, options_.player.timeout_ms, options_.player.pool_size)),
      dungeon_upstream_(std::make_unique<framework::transport::UpstreamClientPool>(
          options_.dungeon.host, options_.dungeon.port, options_.dungeon.timeout_ms, options_.dungeon.pool_size)) {}

bool GatewayForwardExecutor::Start(std::string* error_message) {
    return executor_.Start(error_message);
}

bool GatewayForwardExecutor::Forward(common::net::MessageId message_id,
                                     const framework::execution::ExecutionKey& execution_key,
                                     common::net::Packet request_packet,
                                     Completion completion,
                                     std::size_t* shard_index,
                                     std::string* error_message) {
    return executor_.Submit(
            execution_key,
            [this, message_id, request_packet = std::move(request_packet), completion = std::move(completion)]() mutable {
                auto fail = [&completion](std::string error_message) mutable {
                    try {
                        completion(false, {}, std::move(error_message));
                    } catch (...) {
                    }
                };

                try {
                    common::net::Packet response;
                    std::string error;
                    if (!ResolveUpstream(message_id).SendAndReceive(request_packet, &response, &error)) {
                        fail(std::move(error));
                        return;
                    }

                    try {
                        completion(true, std::move(response), {});
                    } catch (const std::exception& exception) {
                        fail(exception.what());
                    } catch (...) {
                        fail("gateway forward completion failed");
                    }
                } catch (const std::exception& exception) {
                    fail(exception.what());
                } catch (...) {
                    fail("gateway forward request failed");
                }
            },
            shard_index,
            error_message);
}

std::optional<std::size_t> GatewayForwardExecutor::PreviewShard(
    const framework::execution::ExecutionKey& execution_key) const {
    return executor_.PreviewShard(execution_key);
}

void GatewayForwardExecutor::StopAccepting() {
    executor_.StopAccepting();
}

bool GatewayForwardExecutor::WaitForDrain(std::chrono::milliseconds timeout) {
    return executor_.WaitForDrain(timeout);
}

void GatewayForwardExecutor::Shutdown() {
    executor_.Shutdown();
}

framework::transport::UpstreamClientPool& GatewayForwardExecutor::ResolveUpstream(common::net::MessageId message_id) {
    switch (message_id) {
    case common::net::MessageId::kLoginRequest:
        return *login_upstream_;
    case common::net::MessageId::kLoadPlayerRequest:
        return *player_upstream_;
    case common::net::MessageId::kEnterDungeonRequest:
    case common::net::MessageId::kSettleDungeonRequest:
        return *dungeon_upstream_;
    default:
        return *login_upstream_;
    }
}

}  // namespace services::gateway
