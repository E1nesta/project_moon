#include "game_server/player/player_service.h"

namespace game_server::player {

PlayerService::PlayerService(login_server::session::SessionRepository& session_repository,
                             PlayerRepository& player_repository,
                             PlayerCacheRepository& player_cache_repository)
    : session_repository_(session_repository),
      player_repository_(player_repository),
      player_cache_repository_(player_cache_repository) {}

LoadPlayerResponse PlayerService::LoadPlayer(const std::string& session_id, std::int64_t player_id) {
    const auto session = session_repository_.FindById(session_id);
    if (!session.has_value() || session->player_id != player_id) {
        return LoadPlayerResponse{false, common::error::ErrorCode::kSessionInvalid, "session invalid", {}, false};
    }

    if (const auto cached_state = player_cache_repository_.FindByPlayerId(player_id); cached_state.has_value()) {
        return LoadPlayerResponse{true, common::error::ErrorCode::kOk, "", *cached_state, true};
    }

    const auto player_state = player_repository_.LoadPlayerState(player_id);
    if (!player_state.has_value()) {
        return LoadPlayerResponse{false, common::error::ErrorCode::kPlayerNotFound, "player not found", {}, false};
    }

    player_cache_repository_.Save(*player_state);
    return LoadPlayerResponse{true, common::error::ErrorCode::kOk, "", *player_state, false};
}

}  // namespace game_server::player
