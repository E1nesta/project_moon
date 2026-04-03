#pragma once

#include "modules/player/domain/player_state.h"

#include <cstdint>
#include <optional>

namespace game_server::player {

// Storage boundary for player state reads from the system of record.
class PlayerRepository {
public:
    virtual ~PlayerRepository() = default;

    [[nodiscard]] virtual std::optional<common::model::PlayerState> LoadPlayerState(std::int64_t player_id) const = 0;
};

}  // namespace game_server::player
