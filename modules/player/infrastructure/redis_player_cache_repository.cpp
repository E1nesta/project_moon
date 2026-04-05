#include "modules/player/infrastructure/redis_player_cache_repository.h"

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

int ChapterIdFromStageId(int stage_id) {
    return stage_id > 0 ? stage_id / 1000 : 0;
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
    std::ostringstream currencies;
    for (std::size_t index = 0; index < player_state.currencies.size(); ++index) {
        if (index > 0) {
            currencies << ';';
        }
        currencies << player_state.currencies[index].currency_type << ':' << player_state.currencies[index].amount;
    }
    std::ostringstream roles;
    for (std::size_t index = 0; index < player_state.role_summaries.size(); ++index) {
        if (index > 0) {
            roles << ';';
        }
        roles << player_state.role_summaries[index].role_id << ':' << player_state.role_summaries[index].level << ':'
              << player_state.role_summaries[index].star;
    }
    return redis->HSet(
        CacheKey(player_state.profile.player_id),
        {{"player_id", std::to_string(player_state.profile.player_id)},
         {"account_id", std::to_string(player_state.profile.account_id)},
         {"server_id", std::to_string(player_state.profile.server_id)},
         {"player_name", player_state.profile.player_name},
         {"nickname", player_state.profile.nickname},
         {"level", std::to_string(player_state.profile.level)},
         {"stamina", std::to_string(player_state.profile.stamina)},
         {"gold", std::to_string(player_state.profile.gold)},
         {"diamond", std::to_string(player_state.profile.diamond)},
         {"main_stage_id", std::to_string(player_state.profile.main_stage_id)},
         {"main_chapter_id", std::to_string(player_state.profile.main_chapter_id)},
         {"fight_power", std::to_string(player_state.profile.fight_power)},
         {"stage_progress", SerializeProgress(player_state)},
         {"currencies", currencies.str()},
         {"role_summaries", roles.str()}},
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
    state.profile.server_id = std::stoi(values->at("server_id"));
    state.profile.player_name = values->at("player_name");
    state.profile.nickname = values->at("nickname");
    state.profile.level = std::stoi(values->at("level"));
    state.profile.stamina = std::stoi(values->at("stamina"));
    state.profile.gold = std::stoll(values->at("gold"));
    state.profile.diamond = std::stoll(values->at("diamond"));
    state.profile.main_stage_id = std::stoi(values->at("main_stage_id"));
    if (const auto iter = values->find("main_chapter_id"); iter != values->end()) {
        state.profile.main_chapter_id = std::stoi(iter->second);
    } else {
        state.profile.main_chapter_id = ChapterIdFromStageId(state.profile.main_stage_id);
    }
    state.profile.fight_power = std::stoll(values->at("fight_power"));
    state.stage_progress = ParseProgress(values->at("stage_progress"));
    for (const auto& token : Split(values->at("currencies"), ';')) {
        const auto parts = Split(token, ':');
        if (parts.size() != 2) {
            continue;
        }
        state.currencies.push_back({parts[0], std::stoll(parts[1])});
    }
    for (const auto& token : Split(values->at("role_summaries"), ';')) {
        const auto parts = Split(token, ':');
        if (parts.size() != 3) {
            continue;
        }
        state.role_summaries.push_back({std::stoi(parts[0]), std::stoi(parts[1]), std::stoi(parts[2])});
    }
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
    for (const auto& progress : player_state.stage_progress) {
        if (!first) {
            output << ';';
        }
        output << progress.stage_id << ':' << progress.best_star << ':' << (progress.is_first_clear ? 1 : 0);
        first = false;
    }
    return output.str();
}

std::vector<common::model::PlayerStageProgress> RedisPlayerCacheRepository::ParseProgress(const std::string& raw_value) {
    std::vector<common::model::PlayerStageProgress> progress_list;
    for (const auto& token : Split(raw_value, ';')) {
        const auto parts = Split(token, ':');
        if (parts.size() != 3) {
            continue;
        }

        common::model::PlayerStageProgress progress;
        progress.stage_id = std::stoi(parts[0]);
        progress.best_star = std::stoi(parts[1]);
        progress.is_first_clear = parts[2] == "1";
        progress_list.push_back(progress);
    }
    return progress_list;
}

}  // namespace game_server::player
