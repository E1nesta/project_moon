#pragma once

#include <cstdint>
#include <string>

namespace common::model {

struct BattleContext {
    std::string battle_id;
    std::int64_t player_id = 0;
    int dungeon_id = 0;
    int cost_stamina = 0;
    int max_star = 3;
    bool settled = false;
};

}  // namespace common::model
