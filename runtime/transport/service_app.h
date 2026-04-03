#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "runtime/protocol/message_id.h"
#include "runtime/protocol/packet.h"
#include "runtime/execution/sharded_request_executor.h"
#include "runtime/transport/service_options.h"
#include "runtime/transport/route_registry.h"
#include "runtime/transport/transport_server.h"

#include <functional>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace framework::protocol {
struct HandlerContext;
}

namespace framework::service {

// Shared service runtime skeleton.
// Concrete server apps should stay as thin adapters: wire dependencies, register routes
// and delegate business orchestration to application services.
class ServiceApp {
public:
    using Middleware = std::function<bool(common::net::MessageId,
                                          const framework::transport::TransportInbound&,
                                          framework::protocol::HandlerContext*,
                                          common::net::Packet*)>;

    ServiceApp(std::string default_service_name, std::string default_config_path);
    virtual ~ServiceApp() = default;

    int Main(int argc, char* argv[]);

protected:
    // Construct infrastructure and storage dependencies for the concrete service.
    virtual bool BuildDependencies(std::string* error_message) = 0;
    // Register thin route adapters that parse requests, invoke application services and map responses.
    virtual void RegisterRoutes() = 0;
    virtual void BuildMiddlewares();
    virtual void OnDisconnect(std::uint64_t /*connection_id*/) {}
    virtual bool RequiresTrustedGateway() const { return false; }
    virtual bool SignsTrustedGatewayRequests() const { return false; }
    virtual bool UsesRequestExecutor() const { return true; }
    virtual void DispatchRequest(common::net::MessageId message_id,
                                 const framework::transport::TransportInbound& inbound,
                                 const framework::protocol::HandlerContext& context,
                                 framework::transport::ResponseCallback response_callback);
    virtual std::string DescribeDispatchTarget(common::net::MessageId message_id,
                                               const framework::protocol::HandlerContext& context) const;
    virtual void StopServiceExecutorsAccepting();
    virtual bool WaitForServiceExecutors(std::chrono::milliseconds timeout);
    virtual void ShutdownServiceExecutors();
    void LogHandlerContext(const framework::protocol::HandlerContext& context) const;

    void AddMiddleware(Middleware middleware);
    Middleware BuildPingMiddleware();
    Middleware BuildContextEnrichmentMiddleware();
    Middleware BuildTrustedGatewayValidationMiddleware();
    Middleware BuildContextValidationMiddleware();
    Middleware BuildLoggingMiddleware();

    [[nodiscard]] common::config::SimpleConfig& Config();
    [[nodiscard]] const common::config::SimpleConfig& Config() const;
    [[nodiscard]] RouteRegistry& Routes();
    [[nodiscard]] const framework::runtime::ServiceCliOptions& Options() const;
    [[nodiscard]] std::string TracePrefix() const;

private:
    void HandlePacket(const framework::transport::TransportInbound& inbound,
                      framework::transport::ResponseCallback response_callback);
    framework::protocol::HandlerContext BuildFallbackContext(const framework::transport::TransportInbound& inbound) const;
    framework::transport::TransportServer::Options BuildTransportOptions() const;
    [[nodiscard]] std::chrono::milliseconds ShutdownGracePeriod() const;
    bool ValidateBaseConfig(std::string* error_message) const;
    bool BuildRequestExecutor(std::string* error_message);

    std::string default_service_name_;
    std::string default_config_path_;
    framework::runtime::ServiceCliOptions options_;
    common::config::SimpleConfig config_;
    RouteRegistry routes_;
    std::vector<Middleware> middlewares_;
    std::unique_ptr<framework::transport::TransportServer> server_;
    std::unique_ptr<framework::execution::ShardedRequestExecutor> request_executor_;
};

}  // namespace framework::service
