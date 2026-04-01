#pragma once

#include "dungeon_server/dungeon/dungeon_config.h"

#include <optional>

namespace dungeon_server::dungeon {

class DungeonConfigRepository {
public:
    virtual ~DungeonConfigRepository() = default;

    [[nodiscard]] virtual std::optional<DungeonConfig> FindByDungeonId(int dungeon_id) const = 0;
};

}  // namespace dungeon_server::dungeon
