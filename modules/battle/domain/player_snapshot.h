#pragma once

#include <cstdint>
#include <vector>

namespace battle_server::battle {

struct BattleRoleSummary {
    int role_id = 0;
    int level = 0;
    int star = 0;
};

// Minimal player projection required by the battle module.
struct PlayerSnapshot {
    std::int64_t player_id = 0;
    int level = 1;
    int stamina = 0;
    std::vector<BattleRoleSummary> role_summaries;
};

}  // namespace battle_server::battle
