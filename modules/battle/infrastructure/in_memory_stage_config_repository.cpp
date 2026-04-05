#include "modules/battle/infrastructure/in_memory_stage_config_repository.h"

namespace battle_server::battle {

InMemoryStageConfigRepository InMemoryStageConfigRepository::FromConfig(const common::config::SimpleConfig& config) {
    StageConfig stage_config;
    stage_config.chapter_id = config.GetInt("demo.chapter_id", config.GetInt("demo.stage_id", 1001) / 1000);
    stage_config.stage_id = config.GetInt("demo.stage_id", 1001);
    stage_config.required_level = config.GetInt("demo.stage_required_level", 1);
    stage_config.cost_stamina = config.GetInt("demo.stage_cost_stamina", 10);
    stage_config.max_star = config.GetInt("demo.stage_max_star", 3);
    stage_config.normal_gold_reward = config.GetInt("demo.stage_normal_gold_reward", 100);
    stage_config.first_clear_diamond_reward = config.GetInt("demo.stage_first_clear_diamond_reward", 50);
    return InMemoryStageConfigRepository(stage_config);
}

InMemoryStageConfigRepository::InMemoryStageConfigRepository(StageConfig stage_config)
    : stage_config_(stage_config) {}

std::optional<StageConfig> InMemoryStageConfigRepository::FindByStageId(int stage_id) const {
    if (stage_id != stage_config_.stage_id) {
        return std::nullopt;
    }
    return stage_config_;
}

}  // namespace battle_server::battle
