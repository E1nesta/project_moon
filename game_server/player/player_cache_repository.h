#pragma once

#include "common/model/player_state.h"

#include <cstdint>
#include <optional>

namespace game_server::player {

class PlayerCacheRepository {
public:
    virtual ~PlayerCacheRepository() = default;

    virtual bool Save(const common::model::PlayerState& player_state) = 0;
    [[nodiscard]] virtual std::optional<common::model::PlayerState> FindByPlayerId(std::int64_t player_id) const = 0;
    virtual bool Invalidate(std::int64_t player_id) = 0;
};

}  // namespace game_server::player
