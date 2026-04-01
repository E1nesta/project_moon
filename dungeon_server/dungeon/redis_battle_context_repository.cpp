#include "dungeon_server/dungeon/redis_battle_context_repository.h"

namespace dungeon_server::dungeon {

RedisBattleContextRepository RedisBattleContextRepository::FromConfig(common::redis::RedisClient& redis_client,
                                                                      const common::config::SimpleConfig& config) {
    return RedisBattleContextRepository(redis_client, config.GetInt("battle.context_ttl_seconds", 3600));
}

RedisBattleContextRepository::RedisBattleContextRepository(common::redis::RedisClient& redis_client, int ttl_seconds)
    : redis_client_(redis_client), ttl_seconds_(ttl_seconds) {}

bool RedisBattleContextRepository::Save(const common::model::BattleContext& battle_context) {
    return redis_client_.HSet(CacheKey(battle_context.battle_id),
                              {{"player_id", std::to_string(battle_context.player_id)},
                               {"dungeon_id", std::to_string(battle_context.dungeon_id)},
                               {"cost_stamina", std::to_string(battle_context.cost_stamina)},
                               {"max_star", std::to_string(battle_context.max_star)},
                               {"settled", battle_context.settled ? "1" : "0"}},
                              ttl_seconds_);
}

std::optional<common::model::BattleContext> RedisBattleContextRepository::FindByBattleId(const std::string& battle_id) const {
    const auto values = redis_client_.HGetAll(CacheKey(battle_id));
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
    return redis_client_.Del(CacheKey(battle_id));
}

std::string RedisBattleContextRepository::CacheKey(const std::string& battle_id) {
    return "battle:ctx:" + battle_id;
}

}  // namespace dungeon_server::dungeon
