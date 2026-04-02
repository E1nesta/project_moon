#include "gateway/gateway_server.h"

#include "common/error/error_code.h"
#include "common/log/logger.h"
#include "common/net/message_id.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"

#include "game_backend.pb.h"

#include <sstream>

namespace gateway {

namespace {

common::net::RequestContext NormalizeContext(const common::net::IncomingPacket& incoming,
                                             const common::net::RequestContext& parsed) {
    auto context = parsed;
    if (context.request_id == 0) {
        context.request_id = incoming.packet.header.request_id;
    }
    if (context.trace_id.empty()) {
        context.trace_id = "gateway-" + std::to_string(context.request_id);
    }
    return context;
}

void LogGateway(const common::net::RequestContext& context, const std::string& message) {
    std::ostringstream output;
    output << "trace_id=" << context.trace_id
           << " request_id=" << context.request_id
           << " session_id=" << context.session_id
           << " player_id=" << context.player_id
           << ' ' << message;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, output.str());
}

}  // namespace

GatewayServer::GatewayServer(common::config::SimpleConfig config)
    : config_(std::move(config)),
      redis_client_(common::redis::ReadConnectionOptions(config_)) {}

bool GatewayServer::Initialize(std::string* error_message) {
    instance_id_ = config_.GetString("instance.id", config_.GetString("service.name", "gateway"));
    if (!redis_client_.Connect(error_message)) {
        return false;
    }

    session_repository_ = std::make_unique<login_server::session::RedisSessionRepository>(
        redis_client_, config_.GetInt("session.ttl_seconds", 3600));

    const auto timeout_ms = config_.GetInt("upstream.timeout_ms", 3000);
    const auto pool_size = config_.GetInt("upstream.pool_size", 2);

    login_upstream_ = std::make_unique<common::net::UpstreamClientPool>(
        config_.GetString("login.upstream.host", "127.0.0.1"),
        config_.GetInt("login.upstream.port", 7100),
        timeout_ms,
        pool_size);
    game_upstream_ = std::make_unique<common::net::UpstreamClientPool>(
        config_.GetString("game.upstream.host", "127.0.0.1"),
        config_.GetInt("game.upstream.port", 7200),
        timeout_ms,
        pool_size);
    dungeon_upstream_ = std::make_unique<common::net::UpstreamClientPool>(
        config_.GetString("dungeon.upstream.host", "127.0.0.1"),
        config_.GetInt("dungeon.upstream.port", 7300),
        timeout_ms,
        pool_size);

    if (!server_.Listen(config_.GetString("host", "0.0.0.0"), config_.GetInt("port", 7000), error_message)) {
        return false;
    }

    server_.SetPacketHandler([this](const common::net::IncomingPacket& incoming) {
        return HandlePacket(incoming);
    });
    server_.SetDisconnectHandler([this](std::uint64_t connection_id) {
        client_bindings_.erase(connection_id);
    });
    return true;
}

int GatewayServer::Run(const std::function<bool()>& keep_running) {
    return server_.Run(keep_running);
}

std::optional<common::net::Packet> GatewayServer::HandlePacket(const common::net::IncomingPacket& incoming) {
    const auto maybe_message_id = common::net::MessageIdFromInt(incoming.packet.header.msg_id);
    common::net::RequestContext fallback_context;
    fallback_context.request_id = incoming.packet.header.request_id;
    fallback_context.trace_id = "gateway-" + std::to_string(incoming.packet.header.request_id);

    if (!maybe_message_id.has_value()) {
        return common::net::BuildErrorPacket(
            fallback_context, common::error::ErrorCode::kBadGateway, "unknown message id");
    }

    if (*maybe_message_id == common::net::MessageId::kPingRequest) {
        game_backend::proto::PingRequest request;
        if (!common::net::ParseMessage(incoming.packet.body, &request)) {
            return common::net::BuildErrorPacket(
                fallback_context, common::error::ErrorCode::kBadGateway, "invalid ping request");
        }
        const auto context = NormalizeContext(incoming, common::net::FromProto(request.context()));
        return common::net::BuildPingResponsePacket(context, "pong");
    }

    common::net::RequestContext context;
    if (!common::net::ExtractRequestContext(*maybe_message_id, incoming.packet.body, &context)) {
        return common::net::BuildErrorPacket(
            fallback_context, common::error::ErrorCode::kBadGateway, "failed to parse request context");
    }
    context = NormalizeContext(incoming, context);

    if (*maybe_message_id != common::net::MessageId::kLoginRequest) {
        const auto binding_iter = client_bindings_.find(incoming.connection_id);
        if (binding_iter != client_bindings_.end()) {
            if (binding_iter->second.session_id != context.session_id || binding_iter->second.player_id != context.player_id) {
                return common::net::BuildErrorPacket(
                    context, common::error::ErrorCode::kSessionInvalid, "connection session binding mismatch");
            }
        } else {
            const auto restored_binding = RestoreBindingFromSession(context);
            if (!restored_binding.has_value()) {
                LogGateway(context, "session restore failed on " + instance_id_);
                return common::net::BuildErrorPacket(
                    context, common::error::ErrorCode::kSessionInvalid, "session restore failed");
            }

            client_bindings_[incoming.connection_id] = *restored_binding;
            LogGateway(context, "bind restored from redis on " + instance_id_);
        }
    }

    common::net::Packet upstream_response;
    std::string error_message;
    auto& upstream = ResolveUpstream(*maybe_message_id);
    LogGateway(context, "forwarding request to upstream");
    if (!upstream.SendAndReceive(incoming.packet, &upstream_response, &error_message)) {
        return common::net::BuildErrorPacket(context, MapUpstreamError(error_message), error_message);
    }

    if (upstream_response.header.request_id != incoming.packet.header.request_id) {
        return common::net::BuildErrorPacket(
            context, common::error::ErrorCode::kBadGateway, "upstream request_id mismatch");
    }

    const auto upstream_message_id = common::net::MessageIdFromInt(upstream_response.header.msg_id);
    if (!upstream_message_id.has_value()) {
        return common::net::BuildErrorPacket(
            context, common::error::ErrorCode::kBadGateway, "upstream returned unknown message id");
    }

    if (*upstream_message_id != common::net::MessageId::kErrorResponse) {
        const auto expected_response = common::net::ExpectedResponseMessageId(*maybe_message_id);
        if (!expected_response.has_value() || *expected_response != *upstream_message_id) {
            return common::net::BuildErrorPacket(
                context, common::error::ErrorCode::kBadGateway, "upstream returned unexpected response type");
        }
    }

    if (*maybe_message_id == common::net::MessageId::kLoginRequest &&
        *upstream_message_id == common::net::MessageId::kLoginResponse) {
        game_backend::proto::LoginResponse response;
        if (!common::net::ParseMessage(upstream_response.body, &response)) {
            return common::net::BuildErrorPacket(
                context, common::error::ErrorCode::kBadGateway, "failed to parse login response");
        }
        if (response.success()) {
            client_bindings_[incoming.connection_id] = {response.session_id(), response.player_id()};
            common::net::RequestContext bound_context = context;
            bound_context.session_id = response.session_id();
            bound_context.player_id = response.player_id();
            LogGateway(bound_context, "login bound on " + instance_id_);
        }
    }

    return upstream_response;
}

common::net::UpstreamClientPool& GatewayServer::ResolveUpstream(common::net::MessageId message_id) {
    switch (message_id) {
    case common::net::MessageId::kLoginRequest:
        return *login_upstream_;
    case common::net::MessageId::kLoadPlayerRequest:
        return *game_upstream_;
    case common::net::MessageId::kEnterDungeonRequest:
    case common::net::MessageId::kSettleDungeonRequest:
        return *dungeon_upstream_;
    default:
        return *login_upstream_;
    }
}

common::error::ErrorCode GatewayServer::MapUpstreamError(const std::string& error_message) const {
    if (error_message == "timeout") {
        return common::error::ErrorCode::kUpstreamTimeout;
    }
    return common::error::ErrorCode::kServiceUnavailable;
}

std::optional<GatewayServer::ClientBinding> GatewayServer::RestoreBindingFromSession(
    const common::net::RequestContext& context) const {
    if (context.session_id.empty() || context.player_id == 0 || session_repository_ == nullptr) {
        return std::nullopt;
    }

    const auto session = session_repository_->FindById(context.session_id);
    if (!session.has_value() || session->player_id != context.player_id) {
        return std::nullopt;
    }

    return ClientBinding{session->session_id, session->player_id};
}

}  // namespace gateway
