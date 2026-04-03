#pragma once

#include "runtime/foundation/error/error_code.h"
#include "runtime/protocol/message_id.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "runtime/session/session_store.h"
#include "runtime/transport/service_app.h"
#include "apps/gateway/forward_executor.h"
#include "apps/gateway/request_router.h"
#include "apps/gateway/rate_limit_service.h"
#include "apps/gateway/session_binding_service.h"
#include "apps/gateway/upstream_response_validator.h"

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
    bool SignsTrustedGatewayRequests() const override { return true; }
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
    // Adapter dispatch step: resolve forwarding metadata and delegate to the forward executor.
    void ForwardGatewayRequest(common::net::MessageId message_id,
                               const framework::transport::TransportInbound& inbound,
                               const framework::protocol::HandlerContext& context,
                               framework::transport::ResponseCallback response_callback);

    std::unique_ptr<common::redis::RedisClientPool> session_redis_pool_;
    std::unique_ptr<common::redis::RedisClientPool> rate_limit_redis_pool_;
    std::unique_ptr<common::session::SessionReader> session_reader_;
    std::unique_ptr<SessionBindingService> session_binding_service_;
    std::unique_ptr<GatewayRateLimitService> rate_limiter_;
    std::unique_ptr<GatewayForwardExecutor> forward_executor_;
    std::unique_ptr<UpstreamResponseValidator> upstream_response_validator_;
    RequestRouter request_router_;
    std::string instance_id_ = "gateway_server";
};

}  // namespace services::gateway
