#include "game_server/player/in_memory_player_repository.h"

namespace game_server::player {

InMemoryPlayerRepository InMemoryPlayerRepository::FromConfig(const common::config::SimpleConfig& config) {
    common::model::PlayerProfile player_profile;
    player_profile.player_id = config.GetInt("demo.player_id", 20001);
    player_profile.account_id = config.GetInt("demo.account_id", 10001);
    player_profile.player_name = config.GetString("demo.player_name", "hero_demo");
    player_profile.level = config.GetInt("demo.level", 10);
    player_profile.stamina = config.GetInt("demo.stamina", 120);
    player_profile.gold = config.GetInt("demo.gold", 1000);
    player_profile.diamond = config.GetInt("demo.diamond", 100);
    return InMemoryPlayerRepository(player_profile);
}

std::optional<common::model::PlayerProfile> InMemoryPlayerRepository::FindByPlayerId(std::int64_t player_id) const {
    const auto iter = players_.find(player_id);
    if (iter == players_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

InMemoryPlayerRepository::InMemoryPlayerRepository(common::model::PlayerProfile player_profile) {
    players_.emplace(player_profile.player_id, std::move(player_profile));
}

}  // namespace game_server::player

