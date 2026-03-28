#pragma once

#include <cstdint>
#include <string>

namespace common::model {

struct PlayerProfile {
    std::int64_t player_id = 0;
    std::int64_t account_id = 0;
    std::string player_name;
    int level = 1;
    int stamina = 0;
    std::int64_t gold = 0;
    std::int64_t diamond = 0;
};

}  // namespace common::model

