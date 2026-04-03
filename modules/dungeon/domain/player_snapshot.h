#pragma once

#include <cstdint>

namespace dungeon_server::dungeon {

// Minimal player projection required by the dungeon module.
struct PlayerSnapshot {
    std::int64_t player_id = 0;
    int level = 1;
    int stamina = 0;
};

}  // namespace dungeon_server::dungeon
