#pragma once

namespace common::model {

struct PlayerDungeonProgress {
    int dungeon_id = 0;
    int best_star = 0;
    bool is_first_clear = false;
};

}  // namespace common::model
