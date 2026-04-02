#pragma once

#include "common/net/message_id.h"
#include "common/net/packet.h"
#include "framework/execution/sharded_request_executor.h"
#include "framework/transport/transport_client.h"

#include <functional>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace services::gateway {

// Isolates gateway forwarding from transport IO threads and preserves per-key serial execution.
class GatewayForwardExecutor {
public:
    struct UpstreamOptions {
        std::string host = "127.0.0.1";
        int port = 0;
        int timeout_ms = 3000;
        int pool_size = 2;
    };

    struct Options {
        std::size_t worker_threads = 4;
        std::size_t queue_limit = 1024;
        UpstreamOptions login;
        UpstreamOptions player;
        UpstreamOptions dungeon;
    };

    using Completion = std::function<void(bool success,
                                          common::net::Packet response,
                                          std::string error_message)>;

    explicit GatewayForwardExecutor(Options options);

    bool Start(std::string* error_message = nullptr);
    bool Forward(common::net::MessageId message_id,
                 const framework::execution::ExecutionKey& execution_key,
                 common::net::Packet request_packet,
                 Completion completion,
                 std::size_t* shard_index = nullptr,
                 std::string* error_message = nullptr);
    [[nodiscard]] std::optional<std::size_t> PreviewShard(
        const framework::execution::ExecutionKey& execution_key) const;
    void StopAccepting();
    // Wait for already submitted upstream tasks to finish within the shutdown grace period.
    [[nodiscard]] bool WaitForDrain(std::chrono::milliseconds timeout);
    void Shutdown();

private:
    framework::transport::UpstreamClientPool& ResolveUpstream(common::net::MessageId message_id);

    Options options_;
    framework::execution::ShardedRequestExecutor executor_;
    std::unique_ptr<framework::transport::UpstreamClientPool> login_upstream_;
    std::unique_ptr<framework::transport::UpstreamClientPool> player_upstream_;
    std::unique_ptr<framework::transport::UpstreamClientPool> dungeon_upstream_;
};

}  // namespace services::gateway
