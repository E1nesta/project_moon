#pragma once

#include "modules/dungeon/domain/reward.h"

#include <cstdint>
#include <string>
#include <vector>

namespace common::model {

struct BattleContext {
    std::int64_t session_id = 0;
    std::int64_t player_id = 0;
    int stage_id = 0;
    std::string mode = "pve";
    int cost_energy = 0;
    int remain_energy_after = 0;
    std::int64_t seed = 0;
    bool settled = false;
    std::int64_t reward_grant_id = 0;
    int grant_status = 0;
    std::vector<Reward> rewards;
};

}  // namespace common::model
