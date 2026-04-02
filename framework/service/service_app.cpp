#include "framework/service/service_app.h"

#include "common/build/build_info.h"
#include "common/error/error_code.h"
#include "common/log/logger.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"
#include "framework/execution/execution_types.h"
#include "framework/protocol/context_enrichment.h"
#include "framework/protocol/context_extractor.h"
#include "framework/protocol/error_responder.h"
#include "framework/protocol/handler_context.h"
#include "framework/protocol/message_policy_registry.h"
#include "framework/protocol/packet_codec.h"

#include "game_backend.pb.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
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
    logger.SetServiceName(options_.service_name);
    if (!config_.LoadFromFile(options_.config_path)) {
        logger.Log(common::log::LogLevel::kError, "failed to load config file: " + options_.config_path);
        return 1;
    }

    logger.SetServiceName(config_.GetString("service.name", options_.service_name));

    std::string error_message;
    if (!BuildDependencies(&error_message)) {
        logger.Log(common::log::LogLevel::kError, "service dependency init failed: " + error_message);
        return 1;
    }

    if (!BuildRequestExecutor(&error_message)) {
        logger.Log(common::log::LogLevel::kError, "service executor init failed: " + error_message);
        return 1;
    }

    RegisterRoutes();
    BuildMiddlewares();

    if (options_.check_only) {
        logger.Log(common::log::LogLevel::kInfo, "configuration and dependency check passed");
        ShutdownServiceExecutors();
        return 0;
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
        logger.Log(common::log::LogLevel::kError, "transport start failed: " + error_message);
        return 1;
    }

    logger.Log(common::log::LogLevel::kInfo,
               "service listening on " + host + ':' + std::to_string(port));

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    // Transport stops accepting first. Service-specific executors then get a bounded drain window
    // before worker shutdown so graceful stop does not wait forever.
    const auto code = server_->Run(
        [] { return g_running.load(); },
        [this] {
            StopServiceExecutorsAccepting();
            if (!WaitForServiceExecutors(ShutdownGracePeriod())) {
                common::log::Logger::Instance().Log(
                    common::log::LogLevel::kWarn,
                    "request executor drain timed out after " +
                        std::to_string(ShutdownGracePeriod().count()) + "ms");
            }
            ShutdownServiceExecutors();
        });
    return code;
}

void ServiceApp::BuildMiddlewares() {
    AddMiddleware(BuildPingMiddleware());
    AddMiddleware(BuildContextEnrichmentMiddleware());
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
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kBadGateway, "invalid ping request");
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

        *response = framework::protocol::BuildErrorResponse(
            context->request, common::error::ErrorCode::kBadGateway, error_message);
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
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kBadGateway, "message not supported by service");
            return true;
        }

        if (context->request.request_id == 0) {
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kBadGateway, "missing request_id in request context");
            return true;
        }

        if (policy->requires_session && context->request.session_id.empty()) {
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kBadGateway, "missing session_id in request context");
            return true;
        }

        if (policy->requires_player && context->request.player_id == 0) {
            *response = framework::protocol::BuildErrorResponse(
                context->request, common::error::ErrorCode::kBadGateway, "missing player_id in request context");
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
    std::ostringstream output;
    output << "trace_id=" << context.request.trace_id
           << " request_id=" << context.request.request_id
           << " session_id=" << context.request.session_id
           << " player_id=" << context.request.player_id
           << " connection_id=" << context.connection_id
           << " peer=" << context.peer_address
           << " message=" << common::net::ToString(context.message_id)
           << " executor=" << context.executor_label;
    if (context.executor_shard.has_value()) {
        output << " executor_shard=" << *context.executor_shard;
    }
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, output.str());
}

void ServiceApp::HandlePacket(const framework::transport::TransportInbound& inbound,
                              framework::transport::ResponseCallback response_callback) {
    const auto maybe_message_id = common::net::MessageIdFromInt(inbound.packet.header.msg_id);
    const auto fallback = BuildFallbackContext(inbound);
    if (!maybe_message_id.has_value()) {
        response_callback(framework::protocol::BuildErrorResponse(
            fallback.request, common::error::ErrorCode::kBadGateway, "unknown message id"));
        return;
    }

    std::string error_message;
    const auto maybe_request_context =
        framework::protocol::ExtractRequestContext(*maybe_message_id, inbound.packet, &error_message);
    if (!maybe_request_context.has_value()) {
        response_callback(framework::protocol::BuildErrorResponse(
            fallback.request, common::error::ErrorCode::kBadGateway, error_message));
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
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kServiceUnavailable, exception.what()));
        return;
    } catch (...) {
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
    return options;
}

std::chrono::milliseconds ServiceApp::ShutdownGracePeriod() const {
    return std::chrono::milliseconds(std::max(0, config_.GetInt("runtime.shutdown_grace_ms", 5000)));
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
            context.request, common::error::ErrorCode::kBadGateway, "message not supported by service"));
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
            response_callback(framework::protocol::BuildErrorResponse(
                context.request, common::error::ErrorCode::kServiceUnavailable, exception.what()));
            return;
        } catch (...) {
            response_callback(framework::protocol::BuildErrorResponse(
                context.request, common::error::ErrorCode::kServiceUnavailable, "request dispatch failed"));
            return;
        }

        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kBadGateway, "message not supported by service"));
        return;
    }

    const auto key =
        framework::execution::BuildExecutionKey({policy->execution_key_kind}, context);
    if (!key.has_value()) {
        response_callback(framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kBadGateway, "failed to build execution key"));
        return;
    }

    std::size_t shard_index = 0;
    std::string error_message;
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
                    response_callback(framework::protocol::BuildErrorResponse(
                        context.request, common::error::ErrorCode::kServiceUnavailable, exception.what()));
                    return;
                } catch (...) {
                    response_callback(framework::protocol::BuildErrorResponse(
                        context.request, common::error::ErrorCode::kServiceUnavailable, "request dispatch failed"));
                    return;
                }

                response_callback(framework::protocol::BuildErrorResponse(
                    context.request, common::error::ErrorCode::kBadGateway, "message not supported by service"));
            },
            &shard_index,
            &error_message)) {
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
