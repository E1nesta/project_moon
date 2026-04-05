#include "apps/player/player_server_app.h"

#include "runtime/protocol/proto_mapper.h"
#include "runtime/protocol/adapter_utils.h"

#include "game_backend.pb.h"

namespace services::player {

namespace {

void FillProtoPlayerState(const common::model::PlayerState& state, game_backend::proto::PlayerState* output) {
    if (output == nullptr) {
        return;
    }

    auto* profile = output->mutable_profile();
    profile->set_player_id(state.profile.player_id);
    profile->set_account_id(state.profile.account_id);
    profile->set_player_name(state.profile.player_name);
    profile->set_level(state.profile.level);
    profile->set_stamina(state.profile.stamina);
    profile->set_gold(state.profile.gold);
    profile->set_diamond(state.profile.diamond);
    profile->set_server_id(state.profile.server_id);
    profile->set_nickname(state.profile.nickname);
    profile->set_main_stage_id(state.profile.main_stage_id);
    profile->set_main_chapter_id(state.profile.main_chapter_id);
    profile->set_fight_power(state.profile.fight_power);

    output->clear_stage_progress();
    for (const auto& progress : state.stage_progress) {
        auto* item = output->add_stage_progress();
        item->set_stage_id(progress.stage_id);
        item->set_best_star(progress.best_star);
        item->set_is_first_clear(progress.is_first_clear);
    }

    output->clear_currencies();
    for (const auto& currency : state.currencies) {
        auto* item = output->add_currencies();
        item->set_currency_type(currency.currency_type);
        item->set_amount(currency.amount);
    }

    output->clear_role_summaries();
    for (const auto& role : state.role_summaries) {
        auto* item = output->add_role_summaries();
        item->set_role_id(role.role_id);
        item->set_level(role.level);
        item->set_star(role.star);
    }
}

common::net::Packet BuildLoadPlayerResponsePacket(const framework::protocol::HandlerContext& context,
                                                  const game_server::player::LoadPlayerResponse& result) {
    game_backend::proto::LoadPlayerResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_loaded_from_cache(result.loaded_from_cache);
    FillProtoPlayerState(result.player_state, response.mutable_player_state());

    return common::net::BuildPacket(
        common::net::MessageId::kLoadPlayerResponse, context.request.request_id, response);
}

}  // namespace

PlayerServerApp::PlayerServerApp()
    : framework::service::ServiceApp("player_server", "configs/player_server.conf") {}

bool PlayerServerApp::BuildDependencies(std::string* error_message) {
    mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(Config(), "storage.player.mysql."),
        static_cast<std::size_t>(Config().GetInt("storage.player.mysql.pool_size", 4)));
    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config(), "storage.player.redis."),
        static_cast<std::size_t>(Config().GetInt("storage.player.redis.pool_size", 4)));
    if (!mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    player_repository_ = std::make_unique<game_server::player::MySqlPlayerRepository>(*mysql_pool_);
    player_cache_repository_ = std::make_unique<game_server::player::RedisPlayerCacheRepository>(
        *redis_pool_, Config().GetInt("storage.player.snapshot_ttl_seconds", 300));
    player_service_ = std::make_unique<game_server::player::PlayerService>(*player_repository_, *player_cache_repository_);
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
            return player_service_->LoadPlayer(context.request.player_id);
        },
        BuildLoadPlayerResponsePacket);
}

}  // namespace services::player
