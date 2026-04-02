#include "login_server/login_network_server.h"

#include "common/error/error_code.h"
#include "common/log/logger.h"
#include "common/net/message_id.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"

#include "game_backend.pb.h"

#include <sstream>

namespace login_server {

namespace {

common::net::RequestContext NormalizeContext(const common::net::IncomingPacket& incoming,
                                             const common::net::RequestContext& parsed) {
    auto context = parsed;
    if (context.request_id == 0) {
        context.request_id = incoming.packet.header.request_id;
    }
    if (context.trace_id.empty()) {
        context.trace_id = "login-" + std::to_string(context.request_id);
    }
    return context;
}

void LogRequest(const common::net::RequestContext& context, const std::string& message) {
    std::ostringstream output;
    output << "trace_id=" << context.trace_id
           << " request_id=" << context.request_id
           << ' ' << message;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, output.str());
}

}  // namespace

LoginNetworkServer::LoginNetworkServer(common::config::SimpleConfig config)
    : config_(std::move(config)),
      mysql_client_(common::mysql::ReadConnectionOptions(config_)),
      redis_client_(common::redis::ReadConnectionOptions(config_)) {}

bool LoginNetworkServer::Initialize(std::string* error_message) {
    if (!mysql_client_.Connect(error_message)) {
        return false;
    }
    if (!redis_client_.Connect(error_message)) {
        return false;
    }

    account_repository_ = std::make_unique<auth::MySqlAccountRepository>(mysql_client_);
    session_repository_ = std::make_unique<session::RedisSessionRepository>(
        redis_client_, config_.GetInt("ttl.session_seconds", config_.GetInt("session.ttl_seconds", 3600)));
    login_service_ = std::make_unique<LoginService>(*account_repository_, *session_repository_);

    if (!server_.Listen(config_.GetString("host", "0.0.0.0"), config_.GetInt("port", 7100), error_message)) {
        return false;
    }

    server_.SetPacketHandler([this](const common::net::IncomingPacket& incoming) {
        return HandlePacket(incoming);
    });
    return true;
}

int LoginNetworkServer::Run(const std::function<bool()>& keep_running) {
    return server_.Run(keep_running);
}

std::optional<common::net::Packet> LoginNetworkServer::HandlePacket(const common::net::IncomingPacket& incoming) {
    const auto maybe_message_id = common::net::MessageIdFromInt(incoming.packet.header.msg_id);
    common::net::RequestContext fallback_context;
    fallback_context.request_id = incoming.packet.header.request_id;
    fallback_context.trace_id = "login-" + std::to_string(incoming.packet.header.request_id);

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

    if (*maybe_message_id != common::net::MessageId::kLoginRequest) {
        return common::net::BuildErrorPacket(
            fallback_context, common::error::ErrorCode::kBadGateway, "message not supported by login_server");
    }

    game_backend::proto::LoginRequest request;
    if (!common::net::ParseMessage(incoming.packet.body, &request)) {
        return common::net::BuildErrorPacket(
            fallback_context, common::error::ErrorCode::kBadGateway, "invalid login request");
    }

    const auto context = NormalizeContext(incoming, common::net::FromProto(request.context()));
    LogRequest(context, "handling login request");
    const auto result = login_service_->Login({request.account_name(), request.password()});

    game_backend::proto::LoginResponse response;
    common::net::RequestContext response_context = context;
    response_context.session_id = result.session.session_id;
    response_context.player_id = result.default_player_id;
    common::net::FillProto(response_context, response.mutable_context());
    response.set_success(result.success);
    response.set_error_code(static_cast<int>(result.error_code));
    response.set_error_name(std::string(common::error::ToString(result.error_code)));
    response.set_error_message(result.error_message);
    response.set_player_id(result.default_player_id);
    if (result.success) {
        common::net::FillProto(result.session, &response);
    }

    return common::net::BuildPacket(common::net::MessageId::kLoginResponse, context.request_id, response);
}

}  // namespace login_server
