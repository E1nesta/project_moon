#include "game_server/player/redis_player_cache_repository.h"

#include <sstream>

namespace game_server::player {

namespace {

std::vector<std::string> Split(const std::string& input, char separator) {
    std::vector<std::string> parts;
    std::stringstream stream(input);
    std::string item;
    while (std::getline(stream, item, separator)) {
        if (!item.empty()) {
            parts.emplace_back(std::move(item));
        }
    }
    return parts;
}

}  // namespace

RedisPlayerCacheRepository RedisPlayerCacheRepository::FromConfig(common::redis::RedisClientPool& redis_pool,
                                                                  const common::config::SimpleConfig& config) {
    return RedisPlayerCacheRepository(redis_pool, config.GetInt("storage.player.snapshot_ttl_seconds", 300));
}

RedisPlayerCacheRepository::RedisPlayerCacheRepository(common::redis::RedisClientPool& redis_pool, int ttl_seconds)
    : redis_pool_(redis_pool), ttl_seconds_(ttl_seconds) {}

bool RedisPlayerCacheRepository::Save(const common::model::PlayerState& player_state) {
    auto redis = redis_pool_.Acquire();
    return redis->HSet(
        CacheKey(player_state.profile.player_id),
        {{"player_id", std::to_string(player_state.profile.player_id)},
         {"account_id", std::to_string(player_state.profile.account_id)},
         {"player_name", player_state.profile.player_name},
         {"level", std::to_string(player_state.profile.level)},
         {"stamina", std::to_string(player_state.profile.stamina)},
         {"gold", std::to_string(player_state.profile.gold)},
         {"diamond", std::to_string(player_state.profile.diamond)},
         {"dungeon_progress", SerializeProgress(player_state)}},
        ttl_seconds_);
}

std::optional<common::model::PlayerState> RedisPlayerCacheRepository::FindByPlayerId(std::int64_t player_id) const {
    auto redis = redis_pool_.Acquire();
    const auto values = redis->HGetAll(CacheKey(player_id));
    if (!values.has_value() || values->empty()) {
        return std::nullopt;
    }

    common::model::PlayerState state;
    state.profile.player_id = std::stoll(values->at("player_id"));
    state.profile.account_id = std::stoll(values->at("account_id"));
    state.profile.player_name = values->at("player_name");
    state.profile.level = std::stoi(values->at("level"));
    state.profile.stamina = std::stoi(values->at("stamina"));
    state.profile.gold = std::stoll(values->at("gold"));
    state.profile.diamond = std::stoll(values->at("diamond"));
    state.dungeon_progress = ParseProgress(values->at("dungeon_progress"));
    return state;
}

bool RedisPlayerCacheRepository::Invalidate(std::int64_t player_id) {
    auto redis = redis_pool_.Acquire();
    return redis->Del(CacheKey(player_id));
}

std::string RedisPlayerCacheRepository::CacheKey(std::int64_t player_id) {
    return "player:snapshot:" + std::to_string(player_id);
}

std::string RedisPlayerCacheRepository::SerializeProgress(const common::model::PlayerState& player_state) {
    std::ostringstream output;
    bool first = true;
    for (const auto& progress : player_state.dungeon_progress) {
        if (!first) {
            output << ';';
        }
        output << progress.dungeon_id << ':' << progress.best_star << ':' << (progress.is_first_clear ? 1 : 0);
        first = false;
    }
    return output.str();
}

std::vector<common::model::PlayerDungeonProgress> RedisPlayerCacheRepository::ParseProgress(const std::string& raw_value) {
    std::vector<common::model::PlayerDungeonProgress> progress_list;
    for (const auto& token : Split(raw_value, ';')) {
        const auto parts = Split(token, ':');
        if (parts.size() != 3) {
            continue;
        }

        common::model::PlayerDungeonProgress progress;
        progress.dungeon_id = std::stoi(parts[0]);
        progress.best_star = std::stoi(parts[1]);
        progress.is_first_clear = parts[2] == "1";
        progress_list.push_back(progress);
    }
    return progress_list;
}

}  // namespace game_server::player
