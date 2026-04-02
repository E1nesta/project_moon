#include "services/gateway/gateway_server_app.h"

#include "common/log/logger.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"
#include "framework/protocol/error_responder.h"
#include "framework/protocol/message_policy_registry.h"

#include "game_backend.pb.h"

#include <sstream>
#include <utility>

namespace services::gateway {

namespace {

void LogGateway(const framework::protocol::HandlerContext& context, const std::string& message) {
    std::ostringstream output;
    output << "trace_id=" << context.request.trace_id
           << " request_id=" << context.request.request_id
           << " session_id=" << context.request.session_id
           << " player_id=" << context.request.player_id
           << ' ' << message;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, output.str());
}

}  // namespace

GatewayServerApp::GatewayServerApp()
    : framework::service::ServiceApp("gateway_server", "configs/gateway_server.conf") {}

bool GatewayServerApp::BuildDependencies(std::string* error_message) {
    instance_id_ = Config().GetString("service.instance_id", Config().GetString("service.name", "gateway_server"));

    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config()),
        static_cast<std::size_t>(Config().GetInt("storage.redis.pool_size", 4)));
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    session_repository_ = std::make_unique<login_server::session::RedisSessionRepository>(
        *redis_pool_, Config().GetInt("storage.session.ttl_seconds", 3600));
    session_binding_store_ = std::make_unique<SessionBindingStore>(*session_repository_);

    const auto timeout_ms = Config().GetInt("upstream.timeout_ms", 3000);
    const auto pool_size = Config().GetInt("upstream.pool_size", 2);
    GatewayForwardExecutor::Options options;
    options.worker_threads = static_cast<std::size_t>(Config().GetInt("gateway.forward_workers", 4));
    options.queue_limit = static_cast<std::size_t>(Config().GetInt("gateway.forward_queue_limit", 1024));
    options.login = {Config().GetString("upstream.login.host", "127.0.0.1"),
                     Config().GetInt("upstream.login.port", 7100),
                     timeout_ms,
                     pool_size};
    options.player = {Config().GetString("upstream.player.host", "127.0.0.1"),
                      Config().GetInt("upstream.player.port", 7200),
                      timeout_ms,
                      pool_size};
    options.dungeon = {Config().GetString("upstream.dungeon.host", "127.0.0.1"),
                       Config().GetInt("upstream.dungeon.port", 7300),
                       timeout_ms,
                       pool_size};
    forward_executor_ = std::make_unique<GatewayForwardExecutor>(options);
    return forward_executor_->Start(error_message);
}

void GatewayServerApp::RegisterRoutes() {
}

void GatewayServerApp::BuildMiddlewares() {
    AddMiddleware(BuildPingMiddleware());
    AddMiddleware(BuildContextEnrichmentMiddleware());

    AddMiddleware([this](common::net::MessageId message_id,
                         const framework::transport::TransportInbound& /*inbound*/,
                         framework::protocol::HandlerContext* context,
                         common::net::Packet* response) {
        return TryRestoreSessionBinding(message_id, context, response);
    });

    AddMiddleware(BuildContextValidationMiddleware());
    AddMiddleware(BuildLoggingMiddleware());
}

void GatewayServerApp::OnDisconnect(std::uint64_t connection_id) {
    if (session_binding_store_ != nullptr) {
        session_binding_store_->Unbind(connection_id);
    }
}

common::error::ErrorCode GatewayServerApp::MapUpstreamError(const std::string& error_message) const {
    if (error_message == "timeout") {
        return common::error::ErrorCode::kUpstreamTimeout;
    }
    return common::error::ErrorCode::kServiceUnavailable;
}

std::optional<framework::execution::ExecutionKey> GatewayServerApp::BuildGatewayExecutionKey(
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

    if (!context.request.session_id.empty()) {
        return framework::execution::ExecutionKey{
            framework::execution::ExecutionKeyKind::kSession, context.request.session_id};
    }

    return framework::execution::ExecutionKey{
        framework::execution::ExecutionKeyKind::kConnection, std::to_string(context.connection_id)};
}

common::net::Packet GatewayServerApp::FinalizeForwardResponse(common::net::MessageId message_id,
                                                              const framework::protocol::HandlerContext& context,
                                                              const common::net::Packet& request_packet,
                                                              common::net::Packet upstream_response) {
    if (upstream_response.header.request_id != request_packet.header.request_id) {
        return framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kBadGateway, "upstream request_id mismatch");
    }

    const auto upstream_message_id = common::net::MessageIdFromInt(upstream_response.header.msg_id);
    if (!upstream_message_id.has_value()) {
        return framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kBadGateway, "upstream returned unknown message id");
    }

    const auto policy = framework::protocol::MessagePolicyRegistry::Find(message_id);
    if (*upstream_message_id != common::net::MessageId::kErrorResponse) {
        if (!policy.has_value() || !policy->expected_response.has_value() ||
            *policy->expected_response != *upstream_message_id) {
            return framework::protocol::BuildErrorResponse(
                context.request,
                common::error::ErrorCode::kBadGateway,
                "upstream returned unexpected response type");
        }
    }

    if (message_id == common::net::MessageId::kLoginRequest &&
        *upstream_message_id == common::net::MessageId::kLoginResponse) {
        game_backend::proto::LoginResponse response;
        if (!common::net::ParseMessage(upstream_response.body, &response)) {
            return framework::protocol::BuildErrorResponse(
                context.request, common::error::ErrorCode::kBadGateway, "failed to parse login response");
        }
        if (response.success()) {
            session_binding_store_->Bind(context.connection_id, response.session_id(), response.player_id());
            framework::protocol::HandlerContext bound_context = context;
            bound_context.request.session_id = response.session_id();
            bound_context.request.player_id = response.player_id();
            LogGateway(bound_context, "login bound on " + instance_id_);
        }
    }

    return upstream_response;
}

void GatewayServerApp::DispatchRequest(common::net::MessageId message_id,
                                       const framework::transport::TransportInbound& inbound,
                                       const framework::protocol::HandlerContext& context,
                                       framework::transport::ResponseCallback response_callback) {
    ForwardGatewayRequest(message_id, inbound, context, std::move(response_callback));
}

bool GatewayServerApp::TryRestoreSessionBinding(common::net::MessageId message_id,
                                                framework::protocol::HandlerContext* context,
                                                common::net::Packet* response) const {
    if (message_id == common::net::MessageId::kLoginRequest ||
        message_id == common::net::MessageId::kPingRequest) {
        return false;
    }

    const auto binding_result =
        session_binding_store_->ValidateOrRestore(context->connection_id, &context->request);
    if (binding_result.status == SessionBindingStore::Status::kInvalid) {
        LogGateway(*context, binding_result.reason + " on " + instance_id_);
        *response = framework::protocol::BuildErrorResponse(
            context->request, common::error::ErrorCode::kSessionInvalid, binding_result.reason);
        return true;
    }

    if (binding_result.status == SessionBindingStore::Status::kRestored) {
        LogGateway(*context, "bind restored from redis on " + instance_id_);
    }
    return false;
}

void GatewayServerApp::ForwardGatewayRequest(common::net::MessageId message_id,
                                             const framework::transport::TransportInbound& inbound,
                                             const framework::protocol::HandlerContext& context,
                                             framework::transport::ResponseCallback response_callback) {
    const auto execution_key = BuildGatewayExecutionKey(message_id, context);
    if (!execution_key.has_value()) {
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kBadGateway, "failed to build gateway execution key"));
        return;
    }

    LogGateway(context, "forwarding request to upstream");
    std::size_t shard_index = 0;
    std::string error_message;
    if (!forward_executor_->Forward(
        message_id,
        *execution_key,
        inbound.packet,
        [this, message_id, context, request_packet = inbound.packet, response_callback = std::move(response_callback)](
            bool success,
            common::net::Packet upstream_response,
            std::string error_message) mutable {
            if (!success) {
                response_callback(framework::protocol::BuildErrorResponse(
                    context.request, MapUpstreamError(error_message), error_message));
                return;
            }

            response_callback(FinalizeForwardResponse(
                message_id, context, request_packet, std::move(upstream_response)));
        },
        &shard_index,
        &error_message)) {
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kServiceUnavailable, error_message));
        return;
    }

    auto dispatch_context = context;
    dispatch_context.executor_shard = shard_index;
    dispatch_context.executor_label = "forward-shard-" + std::to_string(shard_index);
    LogHandlerContext(dispatch_context);
}

std::string GatewayServerApp::DescribeDispatchTarget(common::net::MessageId message_id,
                                                     const framework::protocol::HandlerContext& context) const {
    if (message_id == common::net::MessageId::kPingRequest) {
        return "direct";
    }

    const auto execution_key = BuildGatewayExecutionKey(message_id, context);
    if (!execution_key.has_value() || forward_executor_ == nullptr) {
        return "forward-unresolved";
    }

    const auto shard = forward_executor_->PreviewShard(*execution_key);
    if (!shard.has_value()) {
        return "forward-unavailable";
    }
    return "forward-shard-" + std::to_string(*shard);
}

void GatewayServerApp::StopServiceExecutorsAccepting() {
    if (forward_executor_ != nullptr) {
        forward_executor_->StopAccepting();
    }
}

bool GatewayServerApp::WaitForServiceExecutors(std::chrono::milliseconds timeout) {
    if (forward_executor_ != nullptr) {
        return forward_executor_->WaitForDrain(timeout);
    }
    return true;
}

void GatewayServerApp::ShutdownServiceExecutors() {
    if (forward_executor_ != nullptr) {
        forward_executor_->Shutdown();
    }
}

}  // namespace services::gateway
