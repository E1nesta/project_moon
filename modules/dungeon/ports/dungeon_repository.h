#pragma once

#include "modules/dungeon/domain/battle_context.h"
#include "modules/dungeon/domain/dungeon_config.h"
#include "modules/dungeon/domain/player_snapshot.h"
#include "modules/dungeon/domain/reward.h"

#include <optional>
#include <string>
#include <vector>

namespace dungeon_server::dungeon {

enum class DungeonRepositoryError {
    kNone,
    kStorageFailure,
    kStaminaNotEnough,
    kUnfinishedBattleExists,
    kBattleAlreadySettled,
};

struct EnterDungeonResult {
    bool success = false;
    int remain_stamina = 0;
    DungeonRepositoryError error = DungeonRepositoryError::kNone;
    std::string error_message;
    common::model::BattleContext battle_context;
};

struct SettleDungeonResult {
    bool success = false;
    bool first_clear = false;
    DungeonRepositoryError error = DungeonRepositoryError::kNone;
    std::string error_message;
    std::vector<common::model::Reward> rewards;
};

class DungeonRepository {
public:
    virtual ~DungeonRepository() = default;

    [[nodiscard]] virtual std::optional<common::model::BattleContext> FindBattleById(
        const std::string& battle_id) const = 0;
    [[nodiscard]] virtual EnterDungeonResult EnterDungeon(const PlayerSnapshot& player_snapshot,
                                                          const DungeonConfig& dungeon_config,
                                                          const std::string& battle_id) = 0;
    [[nodiscard]] virtual SettleDungeonResult SettleDungeon(const common::model::BattleContext& battle_context,
                                                            const DungeonConfig& dungeon_config,
                                                            int star) = 0;
};

}  // namespace dungeon_server::dungeon
