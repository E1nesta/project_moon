#include "game_server/player/player_service.h"

namespace game_server::player {

PlayerService::PlayerService(PlayerRepository& player_repository) : player_repository_(player_repository) {}

LoadPlayerResponse PlayerService::LoadPlayer(std::int64_t player_id) const {
    const auto player_profile = player_repository_.FindByPlayerId(player_id);
    if (!player_profile.has_value()) {
        return LoadPlayerResponse{false, "player not found", {}};
    }

    return LoadPlayerResponse{true, "", *player_profile};
}

}  // namespace game_server::player

