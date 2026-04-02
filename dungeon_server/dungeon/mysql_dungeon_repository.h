#pragma once

#include "common/model/battle_context.h"
#include "common/model/player_state.h"
#include "common/model/reward.h"
#include "common/mysql/mysql_client_pool.h"
#include "dungeon_server/dungeon/dungeon_config.h"

#include <optional>
#include <string>
#include <vector>

namespace dungeon_server::dungeon {

struct EnterDungeonResult {
    bool success = false;
    int remain_stamina = 0;
    std::string error_message;
    common::model::BattleContext battle_context;
};

struct SettleDungeonResult {
    bool success = false;
    bool first_clear = false;
    std::string error_message;
    std::vector<common::model::Reward> rewards;
};

class MySqlDungeonRepository {
public:
    explicit MySqlDungeonRepository(common::mysql::MySqlClientPool& mysql_pool);
    explicit MySqlDungeonRepository(common::mysql::MySqlClient& mysql_client);
    virtual ~MySqlDungeonRepository() = default;

    [[nodiscard]] virtual std::optional<common::model::BattleContext> FindBattleById(const std::string& battle_id) const;
    [[nodiscard]] virtual EnterDungeonResult EnterDungeon(const common::model::PlayerState& player_state,
                                                          const DungeonConfig& dungeon_config,
                                                          const std::string& battle_id);
    [[nodiscard]] virtual SettleDungeonResult SettleDungeon(const common::model::BattleContext& battle_context,
                                                            const DungeonConfig& dungeon_config,
                                                            int star);

private:
    common::mysql::MySqlClientPool* mysql_pool_ = nullptr;
    common::mysql::MySqlClient* mysql_client_ = nullptr;
};

}  // namespace dungeon_server::dungeon
