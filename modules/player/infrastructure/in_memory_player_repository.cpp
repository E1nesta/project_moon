#include "modules/player/infrastructure/in_memory_player_repository.h"

namespace game_server::player {

InMemoryPlayerRepository InMemoryPlayerRepository::FromConfig(const common::config::SimpleConfig& config) {
    common::model::PlayerState player_state;
    player_state.profile.player_id = config.GetInt("demo.player_id", 20001);
    player_state.profile.account_id = config.GetInt("demo.account_id", 10001);
    player_state.profile.player_name = config.GetString("demo.player_name", "hero_demo");
    player_state.profile.level = config.GetInt("demo.level", 10);
    player_state.profile.stamina = config.GetInt("demo.stamina", 120);
    player_state.profile.gold = config.GetInt("demo.gold", 1000);
    player_state.profile.diamond = config.GetInt("demo.diamond", 100);
    return InMemoryPlayerRepository(player_state);
}

std::optional<common::model::PlayerState> InMemoryPlayerRepository::LoadPlayerState(std::int64_t player_id) const {
    const auto iter = players_.find(player_id);
    if (iter == players_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

InMemoryPlayerRepository::InMemoryPlayerRepository(common::model::PlayerState player_state) {
    players_.emplace(player_state.profile.player_id, std::move(player_state));
}

}  // namespace game_server::player
