#include "dungeon_server/dungeon_network_server.h"

#include "common/error/error_code.h"
#include "common/log/logger.h"
#include "common/net/message_id.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"

#include "game_backend.pb.h"

#include <sstream>

namespace dungeon_server {

namespace {

common::net::RequestContext NormalizeContext(const common::net::IncomingPacket& incoming,
                                             const common::net::RequestContext& parsed) {
    auto context = parsed;
    if (context.request_id == 0) {
        context.request_id = incoming.packet.header.request_id;
    }
    if (context.trace_id.empty()) {
        context.trace_id = "dungeon-" + std::to_string(context.request_id);
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

DungeonNetworkServer::DungeonNetworkServer(common::config::SimpleConfig config)
    : config_(std::move(config)),
      mysql_client_(common::mysql::ReadConnectionOptions(config_)),
      redis_client_(common::redis::ReadConnectionOptions(config_)) {}

bool DungeonNetworkServer::Initialize(std::string* error_message) {
    if (!mysql_client_.Connect(error_message)) {
        return false;
    }
    if (!redis_client_.Connect(error_message)) {
        return false;
    }

    session_repository_ = std::make_unique<login_server::session::RedisSessionRepository>(
        redis_client_, config_.GetInt("session.ttl_seconds", 3600));
    player_repository_ = std::make_unique<game_server::player::MySqlPlayerRepository>(mysql_client_);
    player_cache_repository_ = std::make_unique<game_server::player::RedisPlayerCacheRepository>(
        redis_client_, config_.GetInt("player.snapshot_ttl_seconds", 300));
    dungeon_config_repository_ = std::make_unique<dungeon::InMemoryDungeonConfigRepository>(
        dungeon::InMemoryDungeonConfigRepository::FromConfig(config_));
    dungeon_repository_ = std::make_unique<dungeon::MySqlDungeonRepository>(mysql_client_);
    battle_context_repository_ = std::make_unique<dungeon::RedisBattleContextRepository>(
        redis_client_, config_.GetInt("battle.context_ttl_seconds", 3600));
    dungeon_service_ = std::make_unique<dungeon::DungeonService>(*session_repository_,
                                                                 redis_client_,
                                                                 *player_repository_,
                                                                 *player_cache_repository_,
                                                                 *dungeon_config_repository_,
                                                                 *dungeon_repository_,
                                                                 *battle_context_repository_);

    if (!server_.Listen(config_.GetString("host", "0.0.0.0"), config_.GetInt("port", 7300), error_message)) {
        return false;
    }

    server_.SetPacketHandler([this](const common::net::IncomingPacket& incoming) {
        return HandlePacket(incoming);
    });
    return true;
}

int DungeonNetworkServer::Run(const std::function<bool()>& keep_running) {
    return server_.Run(keep_running);
}

std::optional<common::net::Packet> DungeonNetworkServer::HandlePacket(const common::net::IncomingPacket& incoming) {
    const auto maybe_message_id = common::net::MessageIdFromInt(incoming.packet.header.msg_id);
    common::net::RequestContext fallback_context;
    fallback_context.request_id = incoming.packet.header.request_id;
    fallback_context.trace_id = "dungeon-" + std::to_string(incoming.packet.header.request_id);

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

    if (*maybe_message_id == common::net::MessageId::kEnterDungeonRequest) {
        game_backend::proto::EnterDungeonRequest request;
        if (!common::net::ParseMessage(incoming.packet.body, &request)) {
            return common::net::BuildErrorPacket(
                fallback_context, common::error::ErrorCode::kBadGateway, "invalid enter dungeon request");
        }

        auto context = NormalizeContext(incoming, common::net::FromProto(request.context()));
        context.player_id = request.player_id() != 0 ? request.player_id() : context.player_id;
        LogRequest(context, "handling enter dungeon request");
        const auto result = dungeon_service_->EnterDungeon(
            {context.session_id, context.player_id, request.dungeon_id()});

        game_backend::proto::EnterDungeonResponse response;
        common::net::FillProto(context, response.mutable_context());
        response.set_success(result.success);
        response.set_error_code(static_cast<int>(result.error_code));
        response.set_error_name(std::string(common::error::ToString(result.error_code)));
        response.set_error_message(result.error_message);
        response.set_battle_id(result.battle_id);
        response.set_remain_stamina(result.remain_stamina);
        return common::net::BuildPacket(common::net::MessageId::kEnterDungeonResponse, context.request_id, response);
    }

    if (*maybe_message_id == common::net::MessageId::kSettleDungeonRequest) {
        game_backend::proto::SettleDungeonRequest request;
        if (!common::net::ParseMessage(incoming.packet.body, &request)) {
            return common::net::BuildErrorPacket(
                fallback_context, common::error::ErrorCode::kBadGateway, "invalid settle dungeon request");
        }

        auto context = NormalizeContext(incoming, common::net::FromProto(request.context()));
        context.player_id = request.player_id() != 0 ? request.player_id() : context.player_id;
        LogRequest(context, "handling settle dungeon request");
        const auto result = dungeon_service_->SettleDungeon(
            {context.session_id, context.player_id, request.battle_id(), request.dungeon_id(), request.star(), request.result()});

        game_backend::proto::SettleDungeonResponse response;
        common::net::FillProto(context, response.mutable_context());
        response.set_success(result.success);
        response.set_error_code(static_cast<int>(result.error_code));
        response.set_error_name(std::string(common::error::ToString(result.error_code)));
        response.set_error_message(result.error_message);
        response.set_first_clear(result.first_clear);
        for (const auto& reward : result.rewards) {
            common::net::FillProto(reward, response.add_rewards());
        }
        return common::net::BuildPacket(common::net::MessageId::kSettleDungeonResponse, context.request_id, response);
    }

    return common::net::BuildErrorPacket(
        fallback_context, common::error::ErrorCode::kBadGateway, "message not supported by dungeon_server");
}

}  // namespace dungeon_server
