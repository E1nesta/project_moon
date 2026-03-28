#pragma once

#include "common/model/player_profile.h"
#include "game_server/player/player_repository.h"

#include <cstdint>
#include <string>

namespace game_server::player {

struct LoadPlayerResponse {
    bool success = false;
    std::string error_message;
    common::model::PlayerProfile player_profile;
};

class PlayerService {
public:
    explicit PlayerService(PlayerRepository& player_repository);

    [[nodiscard]] LoadPlayerResponse LoadPlayer(std::int64_t player_id) const;

private:
    PlayerRepository& player_repository_;
};

}  // namespace game_server::player

