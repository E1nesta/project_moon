#pragma once

namespace dungeon_server::dungeon {

struct DungeonConfig {
    int dungeon_id = 0;
    int required_level = 1;
    int cost_stamina = 10;
    int max_star = 3;
    int normal_gold_reward = 100;
    int first_clear_diamond_reward = 50;
};

}  // namespace dungeon_server::dungeon
