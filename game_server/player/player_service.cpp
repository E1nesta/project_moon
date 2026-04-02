#include "game_server/player/player_service.h"

namespace game_server::player {

namespace {

LoadPlayerResponse BuildLoadPlayerError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), {}, false};
}

LoadPlayerResponse BuildLoadPlayerSuccess(const common::model::PlayerState& player_state, bool loaded_from_cache) {
    return {true, common::error::ErrorCode::kOk, "", player_state, loaded_from_cache};
}

}  // namespace

PlayerService::PlayerService(login_server::session::SessionRepository& session_repository,
                             PlayerRepository& player_repository,
                             PlayerCacheRepository& player_cache_repository)
    : session_repository_(session_repository),
      player_repository_(player_repository),
      player_cache_repository_(player_cache_repository) {}

LoadPlayerResponse PlayerService::LoadPlayer(const std::string& session_id, std::int64_t player_id) {
    if (!HasValidSession(session_id, player_id)) {
        return BuildLoadPlayerError(common::error::ErrorCode::kSessionInvalid, "session invalid");
    }

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

bool PlayerService::HasValidSession(const std::string& session_id, std::int64_t player_id) const {
    const auto session = session_repository_.FindById(session_id);
    return session.has_value() && session->player_id == player_id;
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
