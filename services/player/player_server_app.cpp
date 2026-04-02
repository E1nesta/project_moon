#include "services/player/player_server_app.h"

#include "common/net/proto_mapper.h"
#include "framework/protocol/adapter_utils.h"

#include "game_backend.pb.h"

namespace services::player {

namespace {

common::net::Packet BuildLoadPlayerResponsePacket(const framework::protocol::HandlerContext& context,
                                                  const game_server::player::LoadPlayerResponse& result) {
    game_backend::proto::LoadPlayerResponse response;
    framework::protocol::FillCommonResponseFields(
        context.request, result.success, result.error_code, result.error_message, &response);
    response.set_loaded_from_cache(result.loaded_from_cache);
    if (result.success) {
        common::net::FillProto(result.player_state, response.mutable_player_state());
    }

    return common::net::BuildPacket(
        common::net::MessageId::kLoadPlayerResponse, context.request.request_id, response);
}

}  // namespace

PlayerServerApp::PlayerServerApp()
    : framework::service::ServiceApp("player_server", "configs/player_server.conf") {}

bool PlayerServerApp::BuildDependencies(std::string* error_message) {
    mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(Config()),
        static_cast<std::size_t>(Config().GetInt("storage.mysql.pool_size", 4)));
    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config()),
        static_cast<std::size_t>(Config().GetInt("storage.redis.pool_size", 4)));
    if (!mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    session_repository_ = std::make_unique<login_server::session::RedisSessionRepository>(
        *redis_pool_, Config().GetInt("storage.session.ttl_seconds", 3600));
    player_repository_ = std::make_unique<game_server::player::MySqlPlayerRepository>(*mysql_pool_);
    player_cache_repository_ = std::make_unique<game_server::player::RedisPlayerCacheRepository>(
        *redis_pool_, Config().GetInt("storage.player.snapshot_ttl_seconds", 300));
    player_service_ = std::make_unique<game_server::player::PlayerService>(
        *session_repository_, *player_repository_, *player_cache_repository_);
    return true;
}

void PlayerServerApp::RegisterRoutes() {
    Routes().Register(common::net::MessageId::kLoadPlayerRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleLoadPlayerRequest(context, packet);
                      });
}

common::net::Packet PlayerServerApp::HandleLoadPlayerRequest(const framework::protocol::HandlerContext& context,
                                                             const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::LoadPlayerRequest>(
        context,
        packet,
        "invalid load player request",
        [this, &context](const game_backend::proto::LoadPlayerRequest& /*request*/) {
            return player_service_->LoadPlayer(context.request.session_id, context.request.player_id);
        },
        BuildLoadPlayerResponsePacket);
}

}  // namespace services::player
