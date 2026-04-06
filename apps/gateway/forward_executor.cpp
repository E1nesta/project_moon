#include "apps/gateway/forward_executor.h"

#include <utility>

namespace services::gateway {

GatewayForwardExecutor::GatewayForwardExecutor(Options options)
    : options_(std::move(options)),
      executor_({options_.worker_threads, options_.worker_threads, options_.queue_limit}),
      login_upstream_(std::make_unique<framework::transport::UpstreamClientPool>(
          options_.login.host,
          options_.login.port,
          options_.login.timeout_ms,
          options_.login.pool_size,
          options_.login.tls)),
      player_upstream_(std::make_unique<framework::transport::UpstreamClientPool>(
          options_.player.host,
          options_.player.port,
          options_.player.timeout_ms,
          options_.player.pool_size,
          options_.player.tls)),
      battle_upstream_(std::make_unique<framework::transport::UpstreamClientPool>(
          options_.battle.host,
          options_.battle.port,
          options_.battle.timeout_ms,
          options_.battle.pool_size,
          options_.battle.tls)) {}

bool GatewayForwardExecutor::Start(std::string* error_message) {
    return executor_.Start(error_message);
}

bool GatewayForwardExecutor::Forward(common::net::MessageId message_id,
                                     const framework::execution::ExecutionKey& execution_key,
                                     common::net::Packet request_packet,
                                     Completion completion,
                                     std::size_t* shard_index,
                                     std::string* error_message,
                                     framework::execution::SubmitFailureCode* submit_failure_code) {
    return executor_.Submit(
            execution_key,
            [this, message_id, request_packet = std::move(request_packet), completion = std::move(completion)]() mutable {
                auto fail = [&completion](std::string error_message,
                                          framework::transport::TransportFailureCode failure_code) mutable {
                    try {
                        completion(false, {}, std::move(error_message), failure_code);
                    } catch (...) {
                    }
                };

                try {
                    common::net::Packet response;
                    std::string error;
                    framework::transport::TransportFailureCode failure_code =
                        framework::transport::TransportFailureCode::kNone;
                    if (!ResolveUpstream(message_id).SendAndReceive(request_packet, &response, &error, &failure_code)) {
                        fail(std::move(error), failure_code);
                        return;
                    }

                    try {
                        completion(true, std::move(response), {}, framework::transport::TransportFailureCode::kNone);
                    } catch (const std::exception& exception) {
                        fail(exception.what(), framework::transport::TransportFailureCode::kNone);
                    } catch (...) {
                        fail("gateway forward completion failed", framework::transport::TransportFailureCode::kNone);
                    }
                } catch (const std::exception& exception) {
                    fail(exception.what(), framework::transport::TransportFailureCode::kNone);
                } catch (...) {
                    fail("gateway forward request failed", framework::transport::TransportFailureCode::kNone);
                }
            },
            shard_index,
            error_message,
            submit_failure_code);
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
    case common::net::MessageId::kEnterBattleRequest:
    case common::net::MessageId::kSettleBattleRequest:
    case common::net::MessageId::kGetActiveBattleRequest:
    case common::net::MessageId::kGetRewardGrantStatusRequest:
        return *battle_upstream_;
    default:
        return *login_upstream_;
    }
}

}  // namespace services::gateway
