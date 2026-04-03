#include "modules/player/application/player_service.h"

namespace game_server::player {

namespace {

LoadPlayerResponse BuildLoadPlayerError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), {}, false};
}

LoadPlayerResponse BuildLoadPlayerSuccess(const common::model::PlayerState& player_state, bool loaded_from_cache) {
    return {true, common::error::ErrorCode::kOk, "", player_state, loaded_from_cache};
}

}  // namespace

PlayerService::PlayerService(PlayerRepository& player_repository,
                             PlayerCacheRepository& player_cache_repository)
    : player_repository_(player_repository),
      player_cache_repository_(player_cache_repository) {}

LoadPlayerResponse PlayerService::LoadPlayer(std::int64_t player_id) {
    if (const auto cached_state = LoadCachedPlayer(player_id); cached_state.has_value()) {
        return BuildLoadSuccess(*cached_state, true);
    }

    const auto player_state = LoadPlayerFromStorage(player_id);
    if (!player_state.has_value()) {
        return BuildLoadPlayerError(common::error::ErrorCode::kPlayerNotFound, "player not found");
    }

    player_cache_repository_.Save(*player_state);
    return BuildLoadSuccess(*player_state, false);
}

std::optional<common::model::PlayerState> PlayerService::LoadCachedPlayer(std::int64_t player_id) const {
    return player_cache_repository_.FindByPlayerId(player_id);
}

std::optional<common::model::PlayerState> PlayerService::LoadPlayerFromStorage(std::int64_t player_id) const {
    return player_repository_.LoadPlayerState(player_id);
}

LoadPlayerResponse PlayerService::BuildLoadSuccess(const common::model::PlayerState& player_state,
                                                   bool loaded_from_cache) const {
    return BuildLoadPlayerSuccess(player_state, loaded_from_cache);
}

}  // namespace game_server::player
