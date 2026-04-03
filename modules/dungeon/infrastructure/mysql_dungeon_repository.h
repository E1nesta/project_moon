#pragma once

#include "modules/dungeon/domain/battle_context.h"
#include "modules/dungeon/domain/reward.h"
#include "modules/dungeon/domain/dungeon_config.h"
#include "modules/dungeon/ports/dungeon_repository.h"
#include "runtime/storage/mysql/mysql_client_pool.h"

#include <optional>
#include <string>
#include <vector>

namespace dungeon_server::dungeon {

class MySqlDungeonRepository final : public DungeonRepository {
public:
    explicit MySqlDungeonRepository(common::mysql::MySqlClientPool& mysql_pool);
    explicit MySqlDungeonRepository(common::mysql::MySqlClient& mysql_client);
    ~MySqlDungeonRepository() override = default;

    [[nodiscard]] std::optional<common::model::BattleContext> FindBattleById(const std::string& battle_id) const override;
    [[nodiscard]] EnterDungeonResult EnterDungeon(const PlayerSnapshot& player_snapshot,
                                                  const DungeonConfig& dungeon_config,
                                                  const std::string& battle_id) override;
    [[nodiscard]] SettleDungeonResult SettleDungeon(const common::model::BattleContext& battle_context,
                                                    const DungeonConfig& dungeon_config,
                                                    int star) override;

private:
    common::mysql::MySqlClientPool* mysql_pool_ = nullptr;
    common::mysql::MySqlClient* mysql_client_ = nullptr;
};

}  // namespace dungeon_server::dungeon
