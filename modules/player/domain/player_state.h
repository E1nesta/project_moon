#pragma once

#include "modules/player/domain/currency_balance.h"
#include "modules/player/domain/player_dungeon_progress.h"
#include "modules/player/domain/player_profile.h"
#include "modules/player/domain/player_role_summary.h"

#include <vector>

namespace common::model {

struct PlayerState {
    PlayerProfile profile;
    std::vector<PlayerDungeonProgress> dungeon_progress;
    std::vector<CurrencyBalance> currencies;
    std::vector<PlayerRoleSummary> role_summaries;
};

}  // namespace common::model
