#pragma once

#include "common/error/error_code.h"
#include "common/net/message_id.h"
#include "common/redis/redis_client_pool.h"
#include "framework/execution/execution_types.h"
#include "framework/service/service_app.h"
#include "login_server/session/redis_session_repository.h"
#include "services/gateway/gateway_forward_executor.h"
#include "services/gateway/session_binding_store.h"

#include <memory>
#include <chrono>
#include <string>

namespace services::gateway {

// Thin adapter for the gateway service process.
class GatewayServerApp : public framework::service::ServiceApp {
public:
    GatewayServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    // Gateway forwards after middleware and does not use the route registry for business dispatch.
    void RegisterRoutes() override;
    void BuildMiddlewares() override;
    void OnDisconnect(std::uint64_t connection_id) override;
    bool UsesRequestExecutor() const override { return false; }
    void DispatchRequest(common::net::MessageId message_id,
                         const framework::transport::TransportInbound& inbound,
                         const framework::protocol::HandlerContext& context,
                         framework::transport::ResponseCallback response_callback) override;
    std::string DescribeDispatchTarget(common::net::MessageId message_id,
                                       const framework::protocol::HandlerContext& context) const override;
    void StopServiceExecutorsAccepting() override;
    bool WaitForServiceExecutors(std::chrono::milliseconds timeout) override;
    void ShutdownServiceExecutors() override;

private:
    // Adapter middleware step: restore session binding from shared state when needed.
    bool TryRestoreSessionBinding(common::net::MessageId message_id,
                                  framework::protocol::HandlerContext* context,
                                  common::net::Packet* response) const;
    // Adapter dispatch step: resolve forwarding metadata and delegate to the forward executor.
    void ForwardGatewayRequest(common::net::MessageId message_id,
                               const framework::transport::TransportInbound& inbound,
                               const framework::protocol::HandlerContext& context,
                               framework::transport::ResponseCallback response_callback);
    common::error::ErrorCode MapUpstreamError(const std::string& error_message) const;
    std::optional<framework::execution::ExecutionKey> BuildGatewayExecutionKey(
        common::net::MessageId message_id,
        const framework::protocol::HandlerContext& context) const;
    common::net::Packet FinalizeForwardResponse(common::net::MessageId message_id,
                                                const framework::protocol::HandlerContext& context,
                                                const common::net::Packet& request_packet,
                                                common::net::Packet upstream_response);

    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<login_server::session::RedisSessionRepository> session_repository_;
    std::unique_ptr<SessionBindingStore> session_binding_store_;
    std::unique_ptr<GatewayForwardExecutor> forward_executor_;
    std::string instance_id_ = "gateway_server";
};

}  // namespace services::gateway
