#pragma once

#include "modules/player/domain/player_dungeon_progress.h"
#include "modules/player/domain/player_profile.h"

#include <vector>

namespace common::model {

struct PlayerState {
    PlayerProfile profile;
    std::vector<PlayerDungeonProgress> dungeon_progress;
};

}  // namespace common::model
