#include "dungeon_server/dungeon/in_memory_dungeon_config_repository.h"

namespace dungeon_server::dungeon {

InMemoryDungeonConfigRepository InMemoryDungeonConfigRepository::FromConfig(const common::config::SimpleConfig& config) {
    DungeonConfig dungeon_config;
    dungeon_config.dungeon_id = config.GetInt("demo.dungeon_id", 1001);
    dungeon_config.required_level = config.GetInt("demo.dungeon_required_level", 1);
    dungeon_config.cost_stamina = config.GetInt("demo.dungeon_cost_stamina", 10);
    dungeon_config.max_star = config.GetInt("demo.dungeon_max_star", 3);
    dungeon_config.normal_gold_reward = config.GetInt("demo.dungeon_normal_gold_reward", 100);
    dungeon_config.first_clear_diamond_reward = config.GetInt("demo.dungeon_first_clear_diamond_reward", 50);
    return InMemoryDungeonConfigRepository(dungeon_config);
}

InMemoryDungeonConfigRepository::InMemoryDungeonConfigRepository(DungeonConfig dungeon_config)
    : dungeon_config_(dungeon_config) {}

std::optional<DungeonConfig> InMemoryDungeonConfigRepository::FindByDungeonId(int dungeon_id) const {
    if (dungeon_id != dungeon_config_.dungeon_id) {
        return std::nullopt;
    }
    return dungeon_config_;
}

}  // namespace dungeon_server::dungeon
