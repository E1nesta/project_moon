#include "modules/dungeon/infrastructure/redis_battle_context_repository.h"

namespace dungeon_server::dungeon {

RedisBattleContextRepository RedisBattleContextRepository::FromConfig(common::redis::RedisClientPool& redis_pool,
                                                                      const common::config::SimpleConfig& config) {
    return RedisBattleContextRepository(redis_pool, config.GetInt("storage.battle.context_ttl_seconds", 3600));
}

RedisBattleContextRepository::RedisBattleContextRepository(common::redis::RedisClientPool& redis_pool, int ttl_seconds)
    : redis_pool_(redis_pool), ttl_seconds_(ttl_seconds) {}

bool RedisBattleContextRepository::Save(const common::model::BattleContext& battle_context) {
    auto redis = redis_pool_.Acquire();
    return redis->HSet(CacheKey(battle_context.session_id),
                       {{"player_id", std::to_string(battle_context.player_id)},
                        {"stage_id", std::to_string(battle_context.stage_id)},
                        {"mode", battle_context.mode},
                        {"cost_energy", std::to_string(battle_context.cost_energy)},
                        {"remain_energy_after", std::to_string(battle_context.remain_energy_after)},
                        {"seed", std::to_string(battle_context.seed)},
                        {"settled", battle_context.settled ? "1" : "0"},
                        {"reward_grant_id", std::to_string(battle_context.reward_grant_id)},
                        {"grant_status", std::to_string(battle_context.grant_status)}},
                       ttl_seconds_);
}

std::optional<common::model::BattleContext> RedisBattleContextRepository::FindByBattleId(std::int64_t session_id) const {
    auto redis = redis_pool_.Acquire();
    const auto values = redis->HGetAll(CacheKey(session_id));
    if (!values.has_value() || values->empty()) {
        return std::nullopt;
    }

    common::model::BattleContext battle_context;
    battle_context.session_id = session_id;
    battle_context.player_id = std::stoll(values->at("player_id"));
    battle_context.stage_id = std::stoi(values->at("stage_id"));
    battle_context.mode = values->at("mode");
    battle_context.cost_energy = std::stoi(values->at("cost_energy"));
    if (const auto iter = values->find("remain_energy_after"); iter != values->end()) {
        battle_context.remain_energy_after = std::stoi(iter->second);
    }
    battle_context.seed = std::stoll(values->at("seed"));
    battle_context.settled = values->at("settled") == "1";
    battle_context.reward_grant_id = std::stoll(values->at("reward_grant_id"));
    battle_context.grant_status = std::stoi(values->at("grant_status"));
    return battle_context;
}

bool RedisBattleContextRepository::Delete(std::int64_t session_id) {
    auto redis = redis_pool_.Acquire();
    return redis->Del(CacheKey(session_id));
}

std::string RedisBattleContextRepository::CacheKey(std::int64_t session_id) {
    return "battle:ctx:" + std::to_string(session_id);
}

}  // namespace dungeon_server::dungeon
