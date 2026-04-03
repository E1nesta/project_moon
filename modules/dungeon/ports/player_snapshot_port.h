#pragma once

#include "modules/dungeon/domain/player_snapshot.h"

#include <cstdint>
#include <optional>

namespace dungeon_server::dungeon {

class PlayerSnapshotPort {
public:
    virtual ~PlayerSnapshotPort() = default;

    [[nodiscard]] virtual std::optional<PlayerSnapshot> LoadPlayerSnapshot(std::int64_t player_id) const = 0;
    virtual bool InvalidatePlayerSnapshot(std::int64_t player_id) = 0;
};

}  // namespace dungeon_server::dungeon
