#include "game_server/game_network_server.h"

#include "common/error/error_code.h"
#include "common/log/logger.h"
#include "common/net/message_id.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"

#include "game_backend.pb.h"

#include <sstream>

namespace game_server {

namespace {

common::net::RequestContext NormalizeContext(const common::net::IncomingPacket& incoming,
                                             const common::net::RequestContext& parsed) {
    auto context = parsed;
    if (context.request_id == 0) {
        context.request_id = incoming.packet.header.request_id;
    }
    if (context.trace_id.empty()) {
        context.trace_id = "game-" + std::to_string(context.request_id);
    }
    return context;
}

void LogRequest(const common::net::RequestContext& context, const std::string& message) {
    std::ostringstream output;
    output << "trace_id=" << context.trace_id
           << " request_id=" << context.request_id
           << " session_id=" << context.session_id
           << " player_id=" << context.player_id
           << ' ' << message;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, output.str());
}

}  // namespace

GameNetworkServer::GameNetworkServer(common::config::SimpleConfig config)
    : config_(std::move(config)),
      mysql_client_(common::mysql::ReadConnectionOptions(config_)),
      redis_client_(common::redis::ReadConnectionOptions(config_)) {}

bool GameNetworkServer::Initialize(std::string* error_message) {
    if (!mysql_client_.Connect(error_message)) {
        return false;
    }
    if (!redis_client_.Connect(error_message)) {
        return false;
    }

    session_repository_ = std::make_unique<login_server::session::RedisSessionRepository>(
        redis_client_, config_.GetInt("ttl.session_seconds", config_.GetInt("session.ttl_seconds", 3600)));
    player_repository_ = std::make_unique<player::MySqlPlayerRepository>(mysql_client_);
    player_cache_repository_ = std::make_unique<player::RedisPlayerCacheRepository>(
        redis_client_, config_.GetInt("ttl.player_snapshot_seconds", config_.GetInt("player.snapshot_ttl_seconds", 300)));
    player_service_ = std::make_unique<player::PlayerService>(
        *session_repository_, *player_repository_, *player_cache_repository_);

    if (!server_.Listen(config_.GetString("host", "0.0.0.0"), config_.GetInt("port", 7200), error_message)) {
        return false;
    }

    server_.SetPacketHandler([this](const common::net::IncomingPacket& incoming) {
        return HandlePacket(incoming);
    });
    return true;
}

int GameNetworkServer::Run(const std::function<bool()>& keep_running) {
    return server_.Run(keep_running);
}

std::optional<common::net::Packet> GameNetworkServer::HandlePacket(const common::net::IncomingPacket& incoming) {
    const auto maybe_message_id = common::net::MessageIdFromInt(incoming.packet.header.msg_id);
    common::net::RequestContext fallback_context;
    fallback_context.request_id = incoming.packet.header.request_id;
    fallback_context.trace_id = "game-" + std::to_string(incoming.packet.header.request_id);

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

    if (*maybe_message_id != common::net::MessageId::kLoadPlayerRequest) {
        return common::net::BuildErrorPacket(
            fallback_context, common::error::ErrorCode::kBadGateway, "message not supported by game_server");
    }

    game_backend::proto::LoadPlayerRequest request;
    if (!common::net::ParseMessage(incoming.packet.body, &request)) {
        return common::net::BuildErrorPacket(
            fallback_context, common::error::ErrorCode::kBadGateway, "invalid load player request");
    }

    auto context = NormalizeContext(incoming, common::net::FromProto(request.context()));
    const auto player_id = request.player_id() != 0 ? request.player_id() : context.player_id;
    context.player_id = player_id;
    LogRequest(context, "handling load player request");
    const auto result = player_service_->LoadPlayer(context.session_id, player_id);

    game_backend::proto::LoadPlayerResponse response;
    common::net::FillProto(context, response.mutable_context());
    response.set_success(result.success);
    response.set_error_code(static_cast<int>(result.error_code));
    response.set_error_name(std::string(common::error::ToString(result.error_code)));
    response.set_error_message(result.error_message);
    response.set_loaded_from_cache(result.loaded_from_cache);
    if (result.success) {
        common::net::FillProto(result.player_state, response.mutable_player_state());
    }

    return common::net::BuildPacket(common::net::MessageId::kLoadPlayerResponse, context.request_id, response);
}

}  // namespace game_server
