#include "dungeon_server/dungeon/redis_battle_context_repository.h"

namespace dungeon_server::dungeon {

RedisBattleContextRepository RedisBattleContextRepository::FromConfig(common::redis::RedisClientPool& redis_pool,
                                                                      const common::config::SimpleConfig& config) {
    return RedisBattleContextRepository(redis_pool, config.GetInt("storage.battle.context_ttl_seconds", 3600));
}

RedisBattleContextRepository::RedisBattleContextRepository(common::redis::RedisClientPool& redis_pool, int ttl_seconds)
    : redis_pool_(redis_pool), ttl_seconds_(ttl_seconds) {}

bool RedisBattleContextRepository::Save(const common::model::BattleContext& battle_context) {
    auto redis = redis_pool_.Acquire();
    return redis->HSet(CacheKey(battle_context.battle_id),
                       {{"player_id", std::to_string(battle_context.player_id)},
                        {"dungeon_id", std::to_string(battle_context.dungeon_id)},
                        {"cost_stamina", std::to_string(battle_context.cost_stamina)},
                        {"max_star", std::to_string(battle_context.max_star)},
                        {"settled", battle_context.settled ? "1" : "0"}},
                       ttl_seconds_);
}

std::optional<common::model::BattleContext> RedisBattleContextRepository::FindByBattleId(const std::string& battle_id) const {
    auto redis = redis_pool_.Acquire();
    const auto values = redis->HGetAll(CacheKey(battle_id));
    if (!values.has_value() || values->empty()) {
        return std::nullopt;
    }

    common::model::BattleContext battle_context;
    battle_context.battle_id = battle_id;
    battle_context.player_id = std::stoll(values->at("player_id"));
    battle_context.dungeon_id = std::stoi(values->at("dungeon_id"));
    battle_context.cost_stamina = std::stoi(values->at("cost_stamina"));
    battle_context.max_star = std::stoi(values->at("max_star"));
    battle_context.settled = values->at("settled") == "1";
    return battle_context;
}

bool RedisBattleContextRepository::Delete(const std::string& battle_id) {
    auto redis = redis_pool_.Acquire();
    return redis->Del(CacheKey(battle_id));
}

std::string RedisBattleContextRepository::CacheKey(const std::string& battle_id) {
    return "battle:ctx:" + battle_id;
}

}  // namespace dungeon_server::dungeon
