#include "apps/gateway/gateway_server_app.h"

#include "runtime/foundation/log/logger.h"
#include "runtime/observability/structured_log.h"
#include "runtime/protocol/proto_codec.h"
#include "runtime/protocol/proto_mapper.h"
#include "runtime/session/redis_session_store.h"
#include "runtime/protocol/error_responder.h"
#include "runtime/protocol/message_policy_registry.h"
#include "runtime/transport/tls_options.h"

#include "game_backend.pb.h"

#include <chrono>
#include <optional>
#include <utility>

namespace services::gateway {

namespace {

void LogGatewayEvent(const framework::protocol::HandlerContext& context,
                     common::log::LogLevel level,
                     framework::observability::LogEvent event,
                     std::string gateway_instance,
                     std::string upstream_service = {},
                     framework::observability::LogErrorCode error_code = framework::observability::LogErrorCode::kNone,
                     std::string detail = {},
                     std::optional<std::int64_t> latency_ms = std::nullopt) {
    auto builder = framework::observability::EventLogBuilder(event);
    builder.WithHandlerContext(context)
        .WithGatewayInstance(gateway_instance)
        .WithUpstreamService(upstream_service)
        .WithErrorCode(error_code)
        .WithDetail(detail);
    if (latency_ms.has_value()) {
        builder.WithLatencyMs(*latency_ms);
    }
    builder.Emit(level);
}

framework::observability::LogErrorCode ClassifyRateLimitError(const GatewayRateLimitService::Decision& decision) {
    return decision.status == GatewayRateLimitService::Status::kUnavailable
               ? framework::observability::LogErrorCode::kRateLimitUnavailable
               : framework::observability::LogErrorCode::kRateLimitHit;
}

framework::transport::TlsOptions ReadScopedTlsOptions(const common::config::SimpleConfig& config,
                                                      const std::string& prefix,
                                                      framework::transport::TlsOptions fallback) {
    if (config.Contains(prefix + "enabled")) {
        fallback.enabled = config.GetBool(prefix + "enabled", fallback.enabled);
    }
    if (config.Contains(prefix + "cert_file")) {
        fallback.cert_file = config.GetString(prefix + "cert_file", fallback.cert_file);
    }
    if (config.Contains(prefix + "key_file")) {
        fallback.key_file = config.GetString(prefix + "key_file", fallback.key_file);
    }
    if (config.Contains(prefix + "ca_file")) {
        fallback.ca_file = config.GetString(prefix + "ca_file", fallback.ca_file);
    }
    if (config.Contains(prefix + "server_name")) {
        fallback.server_name = config.GetString(prefix + "server_name", fallback.server_name);
    }
    if (config.Contains(prefix + "verify_peer")) {
        fallback.verify_peer = config.GetBool(prefix + "verify_peer", fallback.verify_peer);
    }
    return fallback;
}

std::string ExtractLoginAccount(const common::net::Packet& packet) {
    game_backend::proto::LoginRequest request;
    if (!common::net::ParseMessage(packet.body, &request)) {
        return {};
    }
    return request.account_name();
}

}  // namespace

GatewayServerApp::GatewayServerApp()
    : framework::service::ServiceApp("gateway_server", "configs/gateway_server.conf") {}

bool GatewayServerApp::BuildDependencies(std::string* error_message) {
    instance_id_ = Config().GetString("service.instance_id", Config().GetString("service.name", "gateway_server"));

    const auto session_redis_prefix =
        Config().Contains("storage.account.redis.host") ? "storage.account.redis." : "storage.redis.";
    const auto rate_limit_redis_prefix =
        Config().Contains("gateway.rate_limit.redis.host") ? "gateway.rate_limit.redis." : session_redis_prefix;
    const auto redis_options = common::redis::ReadConnectionOptions(Config(), session_redis_prefix);
    session_redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        redis_options,
        static_cast<std::size_t>(Config().GetInt(session_redis_prefix + std::string("pool_size"), 4)));
    rate_limit_redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config(), rate_limit_redis_prefix),
        static_cast<std::size_t>(
            Config().GetInt("gateway.rate_limit.redis.pool_size",
                            Config().GetInt(rate_limit_redis_prefix + std::string("pool_size"), 4))));
    if (!session_redis_pool_->Initialize(error_message)) {
        return false;
    }
    if (!rate_limit_redis_pool_->Initialize(error_message)) {
        return false;
    }

    session_reader_ = std::make_unique<common::session::RedisSessionStore>(
        *session_redis_pool_, Config().GetInt("storage.session.ttl_seconds", 3600));
    session_binding_service_ = std::make_unique<SessionBindingService>(*session_reader_);
    rate_limiter_ =
        std::make_unique<GatewayRateLimitService>(*rate_limit_redis_pool_, GatewayRateLimitService::FromConfig(Config()));

    const auto timeout_ms = Config().GetInt("upstream.timeout_ms", 3000);
    const auto pool_size = Config().GetInt("upstream.pool_size", 2);
    GatewayForwardExecutor::Options options;
    const auto upstream_tls = framework::transport::ReadTlsOptions(Config(), "upstream.tls.");
    const auto login_upstream_tls = ReadScopedTlsOptions(Config(), "upstream.login.tls.", upstream_tls);
    const auto player_upstream_tls = ReadScopedTlsOptions(Config(), "upstream.player.tls.", upstream_tls);
    const auto battle_upstream_tls = ReadScopedTlsOptions(Config(), "upstream.battle.tls.", upstream_tls);
    options.worker_threads = static_cast<std::size_t>(Config().GetInt("gateway.forward_workers", 4));
    options.queue_limit = static_cast<std::size_t>(Config().GetInt("gateway.forward_queue_limit", 1024));
    options.login = {Config().GetString("upstream.login.host", "127.0.0.1"),
                     Config().GetInt("upstream.login.port", 7100),
                     timeout_ms,
                     pool_size,
                     login_upstream_tls};
    options.player = {Config().GetString("upstream.player.host", "127.0.0.1"),
                      Config().GetInt("upstream.player.port", 7200),
                      timeout_ms,
                      pool_size,
                      player_upstream_tls};
    options.battle = {Config().GetString("upstream.battle.host", "127.0.0.1"),
                      Config().GetInt("upstream.battle.port", 7300),
                      timeout_ms,
                      pool_size,
                      battle_upstream_tls};
    forward_executor_ = std::make_unique<GatewayForwardExecutor>(options);
    upstream_response_validator_ = std::make_unique<UpstreamResponseValidator>(*session_binding_service_);
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
        if (message_id == common::net::MessageId::kLoginRequest ||
            message_id == common::net::MessageId::kPingRequest) {
            return false;
        }
        if (context->executor_label.empty()) {
            context->executor_label = DescribeDispatchTarget(message_id, *context);
        }

        const auto binding_result =
            session_binding_service_->ValidateOrRestore(context->connection_id, &context->request);
        if (binding_result.status == SessionBindingService::Status::kInvalid) {
            LogGatewayEvent(*context,
                            common::log::LogLevel::kWarn,
                            framework::observability::LogEvent::kGatewayRequestRejected,
                            instance_id_,
                            framework::observability::DescribeUpstreamService(message_id),
                            framework::observability::ClassifySessionFailure(binding_result.reason),
                            binding_result.reason);
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kSessionInvalid, binding_result.reason);
            return true;
        }

        if (binding_result.status == SessionBindingService::Status::kRestored) {
            LogGatewayEvent(*context,
                            common::log::LogLevel::kInfo,
                            framework::observability::LogEvent::kSessionBindingRestored,
                            instance_id_,
                            framework::observability::DescribeUpstreamService(message_id),
                            framework::observability::LogErrorCode::kNone,
                            "bind restored from redis");
        }
        return false;
    });

    AddMiddleware([this](common::net::MessageId message_id,
                         const framework::transport::TransportInbound& inbound,
                         framework::protocol::HandlerContext* context,
                         common::net::Packet* response) {
        if (rate_limiter_ == nullptr || context == nullptr || response == nullptr) {
            return false;
        }

        if (context->executor_label.empty()) {
            context->executor_label = DescribeDispatchTarget(message_id, *context);
        }
        const auto login_account =
            message_id == common::net::MessageId::kLoginRequest ? ExtractLoginAccount(inbound.packet) : std::string{};
        const auto decision =
            rate_limiter_->Evaluate(message_id, context->request, inbound.peer_address, login_account);
        if (decision.status == GatewayRateLimitService::Status::kAllowed) {
            return false;
        }

        LogGatewayEvent(*context,
                        common::log::LogLevel::kWarn,
                        framework::observability::LogEvent::kGatewayRequestRejected,
                        instance_id_,
                        framework::observability::DescribeUpstreamService(message_id),
                        ClassifyRateLimitError(decision),
                        decision.reason);
        *response = framework::protocol::BuildErrorResponse(
            context->request, common::error::ErrorCode::kRateLimited, decision.reason);
        return true;
    });

    AddMiddleware(BuildContextValidationMiddleware());
    AddMiddleware(BuildLoggingMiddleware());
}

void GatewayServerApp::OnDisconnect(std::uint64_t connection_id) {
    if (session_binding_service_ != nullptr) {
        session_binding_service_->Unbind(connection_id);
    }
}

void GatewayServerApp::DispatchRequest(common::net::MessageId message_id,
                                       const framework::transport::TransportInbound& inbound,
                                       const framework::protocol::HandlerContext& context,
                                       framework::transport::ResponseCallback response_callback) {
    ForwardGatewayRequest(message_id, inbound, context, std::move(response_callback));
}

void GatewayServerApp::ForwardGatewayRequest(common::net::MessageId message_id,
                                             const framework::transport::TransportInbound& inbound,
                                             const framework::protocol::HandlerContext& context,
                                             framework::transport::ResponseCallback response_callback) {
    auto log_context = context;
    if (log_context.executor_label.empty()) {
        log_context.executor_label = DescribeDispatchTarget(message_id, log_context);
    }
    const auto upstream_service = framework::observability::DescribeUpstreamService(message_id);
    const auto forward_started_at = std::chrono::steady_clock::now();
    auto trusted_request_packet = inbound.packet;
    if (!common::net::RewriteRequestContext(message_id, context.request, &trusted_request_packet)) {
        LogGatewayEvent(log_context,
                        common::log::LogLevel::kWarn,
                        framework::observability::LogEvent::kGatewayForwardRejected,
                        instance_id_,
                        upstream_service,
                        framework::observability::LogErrorCode::kGatewayContextRewriteFailed,
                        "failed to rewrite trusted request context");
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kBadGateway, "failed to rewrite trusted request context"));
        return;
    }
    std::string error_message;
    const auto gateway_timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    if (!common::net::SignTrustedRequest(message_id,
                                         gateway_timestamp_ms.count(),
                                         Config().GetString("security.trusted_gateway.shared_secret"),
                                         &trusted_request_packet,
                                         &error_message)) {
        LogGatewayEvent(log_context,
                        common::log::LogLevel::kWarn,
                        framework::observability::LogEvent::kGatewayForwardRejected,
                        instance_id_,
                        upstream_service,
                        framework::observability::LogErrorCode::kTrustedGatewaySignFailed,
                        error_message);
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kTrustedGatewayInvalid, error_message));
        return;
    }
    auto validation_request_packet = trusted_request_packet;

    const auto execution_key = request_router_.BuildExecutionKey(message_id, context);
    if (!execution_key.has_value()) {
        LogGatewayEvent(log_context,
                        common::log::LogLevel::kWarn,
                        framework::observability::LogEvent::kGatewayForwardRejected,
                        instance_id_,
                        upstream_service,
                        framework::observability::LogErrorCode::kExecutionKeyUnresolved,
                        "failed to build gateway execution key");
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kRequestContextInvalid, "failed to build gateway execution key"));
        return;
    }

    LogGatewayEvent(log_context,
                    common::log::LogLevel::kInfo,
                    framework::observability::LogEvent::kGatewayForwardStarted,
                    instance_id_,
                    upstream_service,
                    framework::observability::LogErrorCode::kNone,
                    "forwarding request to upstream");
    std::size_t shard_index = 0;
    error_message.clear();
    framework::execution::SubmitFailureCode submit_failure_code = framework::execution::SubmitFailureCode::kNone;
    if (!forward_executor_->Forward(
        message_id,
        *execution_key,
        std::move(trusted_request_packet),
        [this,
         message_id,
         log_context,
         upstream_service,
         forward_started_at,
         request_packet = std::move(validation_request_packet),
         response_callback = std::move(response_callback)](
            bool success,
            common::net::Packet upstream_response,
            std::string error_message,
            framework::transport::TransportFailureCode failure_code) mutable {
            const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - forward_started_at);
            if (!success) {
                LogGatewayEvent(log_context,
                                common::log::LogLevel::kWarn,
                                framework::observability::LogEvent::kGatewayForwardFailed,
                                instance_id_,
                                upstream_service,
                                failure_code != framework::transport::TransportFailureCode::kNone
                                    ? framework::observability::FromTransportFailureCode(failure_code)
                                    : framework::observability::ClassifyTransportError(error_message),
                                error_message,
                                latency_ms.count());
                response_callback(framework::protocol::BuildErrorResponse(
                    log_context.request, upstream_response_validator_->MapForwardError(failure_code, error_message), error_message));
                return;
            }

            auto validated_response =
                upstream_response_validator_->Validate(message_id, log_context, request_packet, std::move(upstream_response));
            const auto maybe_validated_message_id =
                common::net::MessageIdFromInt(validated_response.header.msg_id);
            if (maybe_validated_message_id.has_value() &&
                *maybe_validated_message_id == common::net::MessageId::kErrorResponse) {
                std::string detail = "upstream response validation failed";
                auto error_code = framework::observability::LogErrorCode::kUpstreamResponseInvalid;
                game_backend::proto::ErrorResponse error_response;
                if (common::net::ParseMessage(validated_response.body, &error_response)) {
                    if (!error_response.error_message().empty()) {
                        detail = error_response.error_message();
                    }
                    if (error_response.error_code() !=
                        static_cast<std::int32_t>(common::error::ErrorCode::kUpstreamResponseInvalid)) {
                        error_code = framework::observability::LogErrorCode::kUpstreamRequestFailed;
                    }
                }
                LogGatewayEvent(log_context,
                                common::log::LogLevel::kWarn,
                                framework::observability::LogEvent::kGatewayForwardFailed,
                                instance_id_,
                                upstream_service,
                                error_code,
                                detail,
                                latency_ms.count());
                response_callback(std::move(validated_response));
                return;
            }

            LogGatewayEvent(log_context,
                            common::log::LogLevel::kInfo,
                            framework::observability::LogEvent::kGatewayForwardSucceeded,
                            instance_id_,
                            upstream_service,
                            framework::observability::LogErrorCode::kNone,
                            "forwarding request to upstream succeeded",
                            latency_ms.count());
            response_callback(std::move(validated_response));
        },
        &shard_index,
        &error_message,
        &submit_failure_code)) {
        LogGatewayEvent(log_context,
                        common::log::LogLevel::kWarn,
                        framework::observability::LogEvent::kGatewayForwardRejected,
                        instance_id_,
                        upstream_service,
                        submit_failure_code != framework::execution::SubmitFailureCode::kNone
                            ? framework::observability::FromSubmitFailureCode(submit_failure_code)
                            : framework::observability::ClassifyExecutorError(error_message),
                        error_message,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - forward_started_at)
                            .count());
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
    return request_router_.DescribeDispatchTarget(message_id, context, forward_executor_.get());
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
