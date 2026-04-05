#include "runtime/transport/service_app.h"

#include "runtime/foundation/build/build_info.h"
#include "runtime/foundation/error/error_code.h"
#include "runtime/foundation/log/logger.h"
#include "runtime/protocol/proto_codec.h"
#include "runtime/protocol/proto_mapper.h"
#include "runtime/execution/execution_types.h"
#include "runtime/protocol/context_enrichment.h"
#include "runtime/protocol/context_extractor.h"
#include "runtime/protocol/error_responder.h"
#include "runtime/protocol/handler_context.h"
#include "runtime/protocol/message_policy_registry.h"
#include "runtime/observability/structured_log.h"
#include "runtime/protocol/packet_codec.h"
#include "runtime/transport/tls_options.h"

#include "game_backend.pb.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

namespace framework::protocol {

HandlerContext NormalizeContext(const std::string& trace_prefix,
                               common::net::MessageId message_id,
                               std::uint64_t connection_id,
                               const std::string& peer_address,
                               std::uint64_t request_id,
                               const common::net::RequestContext& parsed) {
    HandlerContext context;
    context.request = parsed;
    context.message_id = message_id;
    context.connection_id = connection_id;
    context.peer_address = peer_address;
    if (context.request.request_id == 0) {
        context.request.request_id = request_id;
    }
    if (context.request.trace_id.empty()) {
        context.request.trace_id = trace_prefix + '-' + std::to_string(context.request.request_id);
    }
    return context;
}

}  // namespace framework::protocol

namespace framework::service {

namespace {

std::atomic_bool g_running{true};

void HandleSignal(int /*signal*/) {
    g_running.store(false);
}

bool IsResolvedExecutorLabel(const std::string& executor_label) {
    return executor_label == "direct" || executor_label == "unavailable" || executor_label == "unresolved" ||
           executor_label == "forward-unavailable" || executor_label == "forward-unresolved";
}

bool ValidateTlsConfig(const framework::transport::TlsOptions& options,
                       const std::string& prefix,
                       bool require_cert_and_key,
                       std::string* error_message) {
    if (!options.enabled) {
        return true;
    }

    if (require_cert_and_key && (options.cert_file.empty() || options.key_file.empty())) {
        if (error_message != nullptr) {
            *error_message = prefix + "cert_file and " + prefix + "key_file are required when TLS is enabled";
        }
        return false;
    }

    if (!options.cert_file.empty() && !std::filesystem::exists(options.cert_file)) {
        if (error_message != nullptr) {
            *error_message = prefix + "cert_file does not exist";
        }
        return false;
    }

    if (!options.key_file.empty() && !std::filesystem::exists(options.key_file)) {
        if (error_message != nullptr) {
            *error_message = prefix + "key_file does not exist";
        }
        return false;
    }

    if (options.verify_peer && options.ca_file.empty()) {
        if (error_message != nullptr) {
            *error_message = prefix + "ca_file is required when verify_peer is enabled";
        }
        return false;
    }

    if (!options.ca_file.empty() && !std::filesystem::exists(options.ca_file)) {
        if (error_message != nullptr) {
            *error_message = prefix + "ca_file does not exist";
        }
        return false;
    }

    return true;
}

void LogDispatchFailure(const framework::protocol::HandlerContext& context,
                        common::log::LogLevel level,
                        framework::observability::LogEvent event,
                        framework::observability::LogErrorCode error_code,
                        std::string detail) {
    framework::observability::EventLogBuilder(event)
        .WithHandlerContext(context)
        .WithErrorCode(error_code)
        .WithDetail(detail)
        .Emit(level);
}

void LogDispatchFailure(const framework::protocol::HandlerContext& context,
                        common::log::LogLevel level,
                        framework::observability::LogEvent event,
                        framework::observability::LogErrorCode error_code,
                        std::string detail,
                        std::uint32_t raw_message_id) {
    framework::observability::EventLogBuilder(event)
        .AddField("trace_id", context.request.trace_id)
        .AddField("request_id", context.request.request_id)
        .AddField("connection_id", context.connection_id)
        .AddField("peer.address", context.peer_address)
        .AddField("message_id.raw", static_cast<std::uint64_t>(raw_message_id))
        .WithErrorCode(error_code)
        .WithDetail(detail)
        .Emit(level);
}

void LogContextValidationFailure(const framework::protocol::HandlerContext& context,
                                 std::string detail,
                                 framework::observability::LogErrorCode error_code =
                                     framework::observability::LogErrorCode::kRequestContextInvalid) {
    LogDispatchFailure(context,
                       common::log::LogLevel::kWarn,
                       framework::observability::LogEvent::kRequestDispatchRejected,
                       error_code,
                       std::move(detail));
}

void LogExecutorDrainTimeout(std::chrono::milliseconds timeout) {
    framework::observability::EventLogBuilder(framework::observability::LogEvent::kRequestExecutorDrainTimeout)
        .WithErrorCode(framework::observability::LogErrorCode::kExecutorDrainTimeout)
        .WithLatencyMs(static_cast<std::int64_t>(timeout.count()))
        .WithDetail("request executor drain timed out")
        .Emit(common::log::LogLevel::kWarn);
}

void LogServiceLifecycle(common::log::LogLevel level,
                         std::string_view action,
                         std::string_view detail,
                         bool sync = false) {
    framework::observability::EventLogBuilder(framework::observability::LogEvent::kServiceLifecycle)
        .AddField("action", action)
        .WithDetail(detail)
        .Emit(level, sync);
}

}  // namespace

ServiceApp::ServiceApp(std::string default_service_name, std::string default_config_path)
    : default_service_name_(std::move(default_service_name)),
      default_config_path_(std::move(default_config_path)) {}

int ServiceApp::Main(int argc, char* argv[]) {
    g_running.store(true);
    options_ = runtime::ParseServiceOptions(argc, argv, default_service_name_, default_config_path_);
    if (options_.show_version) {
        std::cout << common::build::Version() << '\n';
        return 0;
    }

    auto& logger = common::log::Logger::Instance();
    const auto finalize = [&logger](int code, bool shutdown_logger) {
        logger.Flush();
        if (shutdown_logger) {
            logger.Shutdown();
        }
        return code;
    };
    logger.SetServiceName(options_.service_name);
    if (!config_.LoadFromFile(options_.config_path)) {
        framework::observability::EventLogBuilder(framework::observability::LogEvent::kServiceLifecycle)
            .AddField("action", "config_load_failed")
            .AddField("config.path", options_.config_path)
            .WithDetail("failed to load config file")
            .Emit(common::log::LogLevel::kError, true);
        return finalize(1, true);
    }

    logger.SetServiceName(config_.GetString("service.name", options_.service_name));
    logger.SetServiceInstanceId(
        config_.GetString("service.instance_id", config_.GetString("service.name", options_.service_name)));
    logger.SetEnvironment(config_.GetString("runtime.environment", "local"));
    if (!logger.SetLogFormat(config_.GetString("log.format", "auto"))) {
        logger.SetLogFormat(common::log::LogFormat::kAuto);
        framework::observability::EventLogBuilder(framework::observability::LogEvent::kServiceLifecycle)
            .AddField("action", "logger_config_invalid")
            .AddField("setting", "log.format")
            .AddField("value", config_.GetString("log.format"))
            .AddField("fallback", "auto")
            .WithDetail("invalid logger setting")
            .Emit(common::log::LogLevel::kWarn, true);
    }
    if (!logger.SetMinLogLevel(config_.GetString("log.level", "info"))) {
        logger.SetMinLogLevel(common::log::LogLevel::kInfo);
        framework::observability::EventLogBuilder(framework::observability::LogEvent::kServiceLifecycle)
            .AddField("action", "logger_config_invalid")
            .AddField("setting", "log.level")
            .AddField("value", config_.GetString("log.level"))
            .AddField("fallback", "info")
            .WithDetail("invalid logger setting")
            .Emit(common::log::LogLevel::kWarn, true);
    }

    std::string error_message;
    if (!ValidateBaseConfig(&error_message)) {
        LogServiceLifecycle(
            common::log::LogLevel::kError, "config_validation_failed", error_message, true);
        return finalize(1, true);
    }

    if (!BuildDependencies(&error_message)) {
        LogServiceLifecycle(
            common::log::LogLevel::kError, "dependency_init_failed", error_message, true);
        return finalize(1, true);
    }

    if (!BuildRequestExecutor(&error_message)) {
        LogServiceLifecycle(
            common::log::LogLevel::kError, "executor_init_failed", error_message, true);
        return finalize(1, true);
    }

    RegisterRoutes();
    BuildMiddlewares();

    if (options_.check_only) {
        LogServiceLifecycle(common::log::LogLevel::kInfo, "check_passed", "configuration and dependency check passed");
        ShutdownServiceExecutors();
        return finalize(0, true);
    }

    server_ = std::make_unique<framework::transport::TransportServer>(BuildTransportOptions());
    server_->SetPacketHandler([this](const framework::transport::TransportInbound& inbound,
                                     framework::transport::ResponseCallback response_callback) {
        HandlePacket(inbound, std::move(response_callback));
    });
    server_->SetDisconnectHandler([this](std::uint64_t connection_id) {
        OnDisconnect(connection_id);
    });

    const auto host = config_.GetString("service.listen.host", "0.0.0.0");
    const auto port = config_.GetInt("service.listen.port", 0);
    if (!server_->Start(host, port, &error_message)) {
        LogServiceLifecycle(common::log::LogLevel::kError, "transport_start_failed", error_message, true);
        return finalize(1, true);
    }

    framework::observability::EventLogBuilder(framework::observability::LogEvent::kServiceLifecycle)
        .AddField("action", "listening")
        .AddField("listen.host", host)
        .AddField("listen.port", static_cast<std::int64_t>(port))
        .WithDetail("service listening")
        .Emit(common::log::LogLevel::kInfo);

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    // Transport stops accepting first. Service-specific executors then get a bounded drain window
    // before worker shutdown so graceful stop does not wait forever.
    const auto code = server_->Run(
        [] { return g_running.load(); },
        [this] {
            StopServiceExecutorsAccepting();
            if (!WaitForServiceExecutors(ShutdownGracePeriod())) {
                LogExecutorDrainTimeout(ShutdownGracePeriod());
            }
            ShutdownServiceExecutors();
        });
    return finalize(code, true);
}

void ServiceApp::BuildMiddlewares() {
    AddMiddleware(BuildPingMiddleware());
    AddMiddleware(BuildContextEnrichmentMiddleware());
    if (RequiresTrustedGateway()) {
        AddMiddleware(BuildTrustedGatewayValidationMiddleware());
    }
    AddMiddleware(BuildContextValidationMiddleware());
    AddMiddleware(BuildLoggingMiddleware());
}

void ServiceApp::AddMiddleware(Middleware middleware) {
    middlewares_.push_back(std::move(middleware));
}

ServiceApp::Middleware ServiceApp::BuildPingMiddleware() {
    return [](common::net::MessageId message_id,
              const framework::transport::TransportInbound& inbound,
              framework::protocol::HandlerContext* context,
              common::net::Packet* response) {
        if (message_id != common::net::MessageId::kPingRequest) {
            return false;
        }

        game_backend::proto::PingRequest request;
        if (!common::net::ParseMessage(inbound.packet.body, &request)) {
            LogContextValidationFailure(*context, "invalid ping request");
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kRequestContextInvalid, "invalid ping request");
            return true;
        }

        context->executor_label = "direct";
        *response = common::net::BuildPingResponsePacket(context->request, "pong");
        return true;
    };
}

ServiceApp::Middleware ServiceApp::BuildContextEnrichmentMiddleware() {
    return [](common::net::MessageId message_id,
              const framework::transport::TransportInbound& inbound,
              framework::protocol::HandlerContext* context,
              common::net::Packet* response) {
        std::string error_message;
        if (framework::protocol::EnrichContext(message_id, inbound.packet, context, &error_message)) {
            return false;
        }

        LogContextValidationFailure(*context, error_message);
        *response = framework::protocol::BuildErrorResponse(
            context->request, common::error::ErrorCode::kRequestContextInvalid, error_message);
        return true;
    };
}

ServiceApp::Middleware ServiceApp::BuildTrustedGatewayValidationMiddleware() {
    const auto shared_secret = config_.GetString("security.trusted_gateway.shared_secret");
    const auto max_clock_skew_ms =
        static_cast<std::int64_t>(std::max(0, config_.GetInt("security.trusted_gateway.max_clock_skew_ms", 10000)));

    return [shared_secret, max_clock_skew_ms](common::net::MessageId message_id,
                                              const framework::transport::TransportInbound& inbound,
                                              framework::protocol::HandlerContext* context,
                                              common::net::Packet* response) {
        if (message_id == common::net::MessageId::kPingRequest) {
            return false;
        }

        std::string error_message;
        if (common::net::ValidateTrustedRequest(
                message_id, max_clock_skew_ms, shared_secret, inbound.packet, &error_message)) {
            return false;
        }

        context->executor_label = "trusted-gateway-validation";
        LogDispatchFailure(*context,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kTrustedGatewayValidationFailed,
                           framework::observability::LogErrorCode::kTrustedGatewayValidationFailed,
                           "trusted gateway validation failed: " + error_message);
        *response = framework::protocol::BuildErrorResponse(
            context->request, common::error::ErrorCode::kTrustedGatewayInvalid, error_message);
        return true;
    };
}

ServiceApp::Middleware ServiceApp::BuildContextValidationMiddleware() {
    return [](common::net::MessageId message_id,
              const framework::transport::TransportInbound& /*inbound*/,
              framework::protocol::HandlerContext* context,
              common::net::Packet* response) {
        const auto policy = framework::protocol::MessagePolicyRegistry::Find(message_id);
        if (!policy.has_value()) {
            LogContextValidationFailure(*context,
                                        "message not supported by service",
                                        framework::observability::LogErrorCode::kMessageNotSupported);
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kMessageNotSupported, "message not supported by service");
            return true;
        }

        if (context->request.request_id == 0) {
            LogContextValidationFailure(*context, "missing request_id in request context");
            *response = framework::protocol::BuildErrorResponse(
                context->request,
                common::error::ErrorCode::kRequestContextInvalid,
                "missing request_id in request context");
            return true;
        }

        if (policy->requires_auth_token && context->request.auth_token.empty()) {
            LogContextValidationFailure(*context, "missing auth_token in request context");
            *response = framework::protocol::BuildErrorResponse(
                context->request,
                common::error::ErrorCode::kRequestContextInvalid,
                "missing auth_token in request context");
            return true;
        }

        if (policy->requires_player && context->request.player_id == 0) {
            LogContextValidationFailure(*context, "missing player_id in request context");
            *response = framework::protocol::BuildErrorResponse(
                context->request,
                common::error::ErrorCode::kRequestContextInvalid,
                "missing player_id in request context");
            return true;
        }

        return false;
    };
}

ServiceApp::Middleware ServiceApp::BuildLoggingMiddleware() {
    return [this](common::net::MessageId message_id,
                  const framework::transport::TransportInbound& /*inbound*/,
                  framework::protocol::HandlerContext* context,
                  common::net::Packet* /*response*/) {
        if (context->executor_label.empty()) {
            context->executor_label = DescribeDispatchTarget(message_id, *context);
        }
        if (context->executor_shard.has_value() || IsResolvedExecutorLabel(context->executor_label)) {
            LogHandlerContext(*context);
        }
        return false;
    };
}

common::config::SimpleConfig& ServiceApp::Config() {
    return config_;
}

const common::config::SimpleConfig& ServiceApp::Config() const {
    return config_;
}

RouteRegistry& ServiceApp::Routes() {
    return routes_;
}

const framework::runtime::ServiceCliOptions& ServiceApp::Options() const {
    return options_;
}

std::string ServiceApp::TracePrefix() const {
    return config_.GetString("service.name", options_.service_name);
}

void ServiceApp::LogHandlerContext(const framework::protocol::HandlerContext& context) const {
    framework::observability::EventLogBuilder(framework::observability::LogEvent::kRequestDispatchContext)
        .WithHandlerContext(context)
        .Emit(common::log::LogLevel::kInfo);
}

void ServiceApp::HandlePacket(const framework::transport::TransportInbound& inbound,
                              framework::transport::ResponseCallback response_callback) {
    const auto maybe_message_id = common::net::MessageIdFromInt(inbound.packet.header.msg_id);
    const auto fallback = BuildFallbackContext(inbound);
    if (!maybe_message_id.has_value()) {
        LogDispatchFailure(fallback,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kRequestDispatchRejected,
                           framework::observability::LogErrorCode::kMessageNotSupported,
                           "unknown message id",
                           inbound.packet.header.msg_id);
        response_callback(framework::protocol::BuildErrorResponse(
            fallback.request, common::error::ErrorCode::kMessageNotSupported, "unknown message id"));
        return;
    }

    std::string error_message;
    const auto maybe_request_context =
        framework::protocol::ExtractRequestContext(*maybe_message_id, inbound.packet, &error_message);
    if (!maybe_request_context.has_value()) {
        auto log_context = fallback;
        log_context.message_id = *maybe_message_id;
        LogDispatchFailure(log_context,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kRequestDispatchRejected,
                           framework::observability::LogErrorCode::kRequestContextInvalid,
                           error_message);
        response_callback(framework::protocol::BuildErrorResponse(
            fallback.request, common::error::ErrorCode::kRequestContextInvalid, error_message));
        return;
    }

    auto context = framework::protocol::NormalizeContext(
        TracePrefix(), *maybe_message_id, inbound.connection_id, inbound.peer_address, inbound.packet.header.request_id,
        *maybe_request_context);

    try {
        // Middleware still executes on the request handling path before work is handed off to
        // executors. It should enrich context, validate protocol rules and short-circuit direct
        // responses, but not perform business orchestration.
        for (const auto& middleware : middlewares_) {
            common::net::Packet response;
            if (middleware(*maybe_message_id, inbound, &context, &response)) {
                response_callback(std::move(response));
                return;
            }
        }
    } catch (const std::exception& exception) {
        LogDispatchFailure(context,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kRequestDispatchFailed,
                           framework::observability::LogErrorCode::kRequestDispatchFailed,
                           exception.what());
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kServiceUnavailable, exception.what()));
        return;
    } catch (...) {
        LogDispatchFailure(context,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kRequestDispatchFailed,
                           framework::observability::LogErrorCode::kRequestDispatchFailed,
                           "request middleware failed");
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kServiceUnavailable, "request middleware failed"));
        return;
    }

    DispatchRequest(*maybe_message_id, inbound, context, std::move(response_callback));
}

framework::protocol::HandlerContext ServiceApp::BuildFallbackContext(
    const framework::transport::TransportInbound& inbound) const {
    common::net::RequestContext request;
    request.request_id = inbound.packet.header.request_id;
    request.trace_id = TracePrefix() + '-' + std::to_string(inbound.packet.header.request_id);
    framework::protocol::HandlerContext context;
    context.request = std::move(request);
    context.connection_id = inbound.connection_id;
    context.peer_address = inbound.peer_address;
    return context;
}

framework::transport::TransportServer::Options ServiceApp::BuildTransportOptions() const {
    framework::transport::TransportServer::Options options;
    options.io_threads = static_cast<std::size_t>(config_.GetInt("transport.io_threads", 1));
    options.idle_timeout_ms = config_.GetInt("transport.idle_timeout_ms", 30000);
    options.write_queue_limit = static_cast<std::size_t>(config_.GetInt("transport.write_queue_limit", 128));
    options.max_packet_body_bytes =
        static_cast<std::uint32_t>(config_.GetInt(
            "transport.max_packet_body_bytes",
            static_cast<int>(framework::protocol::kDefaultMaxPacketBodyBytes)));
    options.shutdown_grace_ms = config_.GetInt("runtime.shutdown_grace_ms", 5000);
    options.tls = framework::transport::ReadTlsOptions(config_, "transport.tls.");
    return options;
}

std::chrono::milliseconds ServiceApp::ShutdownGracePeriod() const {
    return std::chrono::milliseconds(std::max(0, config_.GetInt("runtime.shutdown_grace_ms", 5000)));
}

bool ServiceApp::ValidateBaseConfig(std::string* error_message) const {
    if ((RequiresTrustedGateway() || SignsTrustedGatewayRequests()) &&
        config_.GetString("security.trusted_gateway.shared_secret").empty()) {
        if (error_message != nullptr) {
            *error_message = "security.trusted_gateway.shared_secret is required";
        }
        return false;
    }

    if ((RequiresTrustedGateway() || SignsTrustedGatewayRequests()) &&
        config_.GetInt("security.trusted_gateway.max_clock_skew_ms", 10000) <= 0) {
        if (error_message != nullptr) {
            *error_message = "security.trusted_gateway.max_clock_skew_ms must be > 0";
        }
        return false;
    }

    if (config_.GetString("runtime.environment", "local") == "prod") {
        if (config_.Contains("storage.mysql.password")) {
            const auto password = config_.GetString("storage.mysql.password");
            if (password.empty() || password == "gamepass") {
                if (error_message != nullptr) {
                    *error_message = "prod requires non-default storage.mysql.password";
                }
                return false;
            }
        }

        if (config_.Contains("storage.redis.host") && config_.GetString("storage.redis.password").empty()) {
            if (error_message != nullptr) {
                *error_message = "prod requires non-empty storage.redis.password";
            }
            return false;
        }
    }

    if (!ValidateTlsConfig(framework::transport::ReadTlsOptions(config_, "transport.tls."),
                           "transport.tls.",
                           true,
                           error_message) ||
        !ValidateTlsConfig(
            framework::transport::ReadTlsOptions(config_, "client.tls."), "client.tls.", false, error_message) ||
        !ValidateTlsConfig(
            framework::transport::ReadTlsOptions(config_, "upstream.tls."), "upstream.tls.", false, error_message) ||
        !ValidateTlsConfig(framework::transport::ReadTlsOptions(config_, "upstream.login.tls."),
                           "upstream.login.tls.",
                           false,
                           error_message) ||
        !ValidateTlsConfig(framework::transport::ReadTlsOptions(config_, "upstream.player.tls."),
                           "upstream.player.tls.",
                           false,
                           error_message) ||
        !ValidateTlsConfig(framework::transport::ReadTlsOptions(config_, "upstream.battle.tls."),
                           "upstream.battle.tls.",
                           false,
                           error_message)) {
        return false;
    }

    return true;
}

bool ServiceApp::BuildRequestExecutor(std::string* error_message) {
    if (!UsesRequestExecutor()) {
        return true;
    }

    framework::execution::ShardedRequestExecutor::Options options;
    options.worker_threads = static_cast<std::size_t>(
        config_.GetInt("execution.worker_threads", config_.GetInt("transport.io_threads", 1)));
    options.shard_count = static_cast<std::size_t>(
        config_.GetInt("execution.shard_count", static_cast<int>(options.worker_threads)));
    options.queue_limit = static_cast<std::size_t>(config_.GetInt("execution.queue_limit", 1024));
    request_executor_ = std::make_unique<framework::execution::ShardedRequestExecutor>(options);
    return request_executor_->Start(error_message);
}

void ServiceApp::DispatchRequest(common::net::MessageId message_id,
                                 const framework::transport::TransportInbound& inbound,
                                 const framework::protocol::HandlerContext& context,
                                 framework::transport::ResponseCallback response_callback) {
    const auto policy = framework::protocol::MessagePolicyRegistry::Find(message_id);
    if (!policy.has_value()) {
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kMessageNotSupported, "message not supported by service"));
        return;
    }

    if (policy->execution_key_kind == framework::execution::ExecutionKeyKind::kDirect || request_executor_ == nullptr) {
        try {
            const auto response = routes_.Dispatcher().Dispatch(message_id, context, inbound.packet);
            if (response.has_value()) {
                response_callback(*response);
                return;
            }
        } catch (const std::exception& exception) {
            LogDispatchFailure(context,
                               common::log::LogLevel::kWarn,
                               framework::observability::LogEvent::kRequestDispatchFailed,
                               framework::observability::ClassifyExecutorError(exception.what()),
                               exception.what());
            response_callback(framework::protocol::BuildErrorResponse(
                context.request, common::error::ErrorCode::kServiceUnavailable, exception.what()));
            return;
        } catch (...) {
            LogDispatchFailure(context,
                               common::log::LogLevel::kWarn,
                               framework::observability::LogEvent::kRequestDispatchFailed,
                               framework::observability::LogErrorCode::kRequestDispatchFailed,
                               "request dispatch failed");
            response_callback(framework::protocol::BuildErrorResponse(
                context.request, common::error::ErrorCode::kServiceUnavailable, "request dispatch failed"));
            return;
        }

        LogDispatchFailure(context,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kRequestDispatchRejected,
                           framework::observability::LogErrorCode::kMessageNotSupported,
                           "message not supported by service");
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kMessageNotSupported, "message not supported by service"));
        return;
    }

    const auto key =
        framework::execution::BuildExecutionKey({policy->execution_key_kind}, context);
    if (!key.has_value()) {
        LogDispatchFailure(context,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kRequestDispatchRejected,
                           framework::observability::LogErrorCode::kExecutionKeyUnresolved,
                           "failed to build execution key");
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kRequestContextInvalid, "failed to build execution key"));
        return;
    }

    std::size_t shard_index = 0;
    std::string error_message;
    framework::execution::SubmitFailureCode submit_failure_code = framework::execution::SubmitFailureCode::kNone;
    if (!request_executor_->Submit(
            *key,
            [dispatcher = &routes_.Dispatcher(), message_id, context, packet = inbound.packet, response_callback]() mutable {
                try {
                    const auto response = dispatcher->Dispatch(message_id, context, packet);
                    if (response.has_value()) {
                        response_callback(*response);
                        return;
                    }
                } catch (const std::exception& exception) {
                    LogDispatchFailure(context,
                                       common::log::LogLevel::kWarn,
                                       framework::observability::LogEvent::kRequestDispatchFailed,
                                       framework::observability::ClassifyExecutorError(exception.what()),
                                       exception.what());
                    response_callback(framework::protocol::BuildErrorResponse(
                        context.request, common::error::ErrorCode::kServiceUnavailable, exception.what()));
                    return;
                } catch (...) {
                    LogDispatchFailure(context,
                                       common::log::LogLevel::kWarn,
                                       framework::observability::LogEvent::kRequestDispatchFailed,
                                       framework::observability::LogErrorCode::kRequestDispatchFailed,
                                       "request dispatch failed");
                    response_callback(framework::protocol::BuildErrorResponse(
                        context.request, common::error::ErrorCode::kServiceUnavailable, "request dispatch failed"));
                    return;
                }

                LogDispatchFailure(context,
                                   common::log::LogLevel::kWarn,
                                   framework::observability::LogEvent::kRequestDispatchRejected,
                                   framework::observability::LogErrorCode::kMessageNotSupported,
                                   "message not supported by service");
                response_callback(framework::protocol::BuildErrorResponse(
                    context.request,
                    common::error::ErrorCode::kMessageNotSupported,
                    "message not supported by service"));
            },
            &shard_index,
            &error_message,
            &submit_failure_code)) {
        LogDispatchFailure(context,
                           common::log::LogLevel::kWarn,
                           framework::observability::LogEvent::kRequestDispatchRejected,
                           submit_failure_code != framework::execution::SubmitFailureCode::kNone
                               ? framework::observability::FromSubmitFailureCode(submit_failure_code)
                               : framework::observability::ClassifyExecutorError(error_message),
                           error_message);
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kServiceUnavailable, error_message));
        return;
    }

    auto dispatch_context = context;
    dispatch_context.executor_shard = shard_index;
    dispatch_context.executor_label = "shard-" + std::to_string(shard_index);
    LogHandlerContext(dispatch_context);
}

std::string ServiceApp::DescribeDispatchTarget(common::net::MessageId message_id,
                                               const framework::protocol::HandlerContext& context) const {
    const auto policy = framework::protocol::MessagePolicyRegistry::Find(message_id);
    if (!policy.has_value() || policy->execution_key_kind == framework::execution::ExecutionKeyKind::kDirect) {
        return "direct";
    }

    if (request_executor_ == nullptr) {
        return "unavailable";
    }

    const auto key =
        framework::execution::BuildExecutionKey({policy->execution_key_kind}, context);
    if (!key.has_value()) {
        return "unresolved";
    }

    const auto shard = request_executor_->PreviewShard(*key);
    if (!shard.has_value()) {
        return "unavailable";
    }
    return "shard-" + std::to_string(*shard);
}

void ServiceApp::StopServiceExecutorsAccepting() {
    if (request_executor_ != nullptr) {
        request_executor_->StopAccepting();
    }
}

bool ServiceApp::WaitForServiceExecutors(std::chrono::milliseconds timeout) {
    if (request_executor_ != nullptr) {
        return request_executor_->WaitForDrain(timeout);
    }
    return true;
}

void ServiceApp::ShutdownServiceExecutors() {
    if (request_executor_ != nullptr) {
        request_executor_->Shutdown();
    }
}

}  // namespace framework::service
