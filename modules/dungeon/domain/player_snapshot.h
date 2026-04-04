#pragma once

#include "modules/player/domain/player_role_summary.h"

#include <cstdint>
#include <vector>

namespace dungeon_server::dungeon {

// Minimal player projection required by the dungeon module.
struct PlayerSnapshot {
    std::int64_t player_id = 0;
    int level = 1;
    int stamina = 0;
    std::vector<common::model::PlayerRoleSummary> role_summaries;
};

}  // namespace dungeon_server::dungeon
