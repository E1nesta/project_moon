#pragma once

#include "common/model/player_dungeon_progress.h"
#include "common/model/player_profile.h"

#include <vector>

namespace common::model {

struct PlayerState {
    PlayerProfile profile;
    std::vector<PlayerDungeonProgress> dungeon_progress;
};

}  // namespace common::model
