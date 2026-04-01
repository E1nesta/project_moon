#pragma once

#include "common/config/simple_config.h"
#include "game_server/player/player_repository.h"

#include <unordered_map>

namespace game_server::player {

class InMemoryPlayerRepository final : public PlayerRepository {
public:
    static InMemoryPlayerRepository FromConfig(const common::config::SimpleConfig& config);

    [[nodiscard]] std::optional<common::model::PlayerState> LoadPlayerState(std::int64_t player_id) const override;

private:
    explicit InMemoryPlayerRepository(common::model::PlayerState player_state);

    std::unordered_map<std::int64_t, common::model::PlayerState> players_;
};

}  // namespace game_server::player
