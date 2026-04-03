#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "modules/dungeon/ports/dungeon_config_repository.h"

namespace dungeon_server::dungeon {

class InMemoryDungeonConfigRepository final : public DungeonConfigRepository {
public:
    static InMemoryDungeonConfigRepository FromConfig(const common::config::SimpleConfig& config);

    explicit InMemoryDungeonConfigRepository(DungeonConfig dungeon_config);

    [[nodiscard]] std::optional<DungeonConfig> FindByDungeonId(int dungeon_id) const override;

private:
    DungeonConfig dungeon_config_;
};

}  // namespace dungeon_server::dungeon
