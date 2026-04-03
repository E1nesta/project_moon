#pragma once

#include "runtime/foundation/error/error_code.h"
#include "modules/dungeon/domain/battle_context.h"
#include "modules/dungeon/domain/reward.h"
#include "modules/dungeon/domain/player_snapshot.h"
#include "modules/dungeon/ports/battle_context_repository.h"
#include "modules/dungeon/ports/dungeon_config_repository.h"
#include "modules/dungeon/ports/dungeon_repository.h"
#include "modules/dungeon/ports/player_snapshot_port.h"
#include "modules/dungeon/ports/player_lock_repository.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace dungeon_server::dungeon {

// Application model for the enter dungeon use case.
struct EnterDungeonRequest {
    std::int64_t player_id = 0;
    int dungeon_id = 0;
};

// Application result for the enter dungeon use case.
struct EnterDungeonResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    std::string battle_id;
    int remain_stamina = 0;
};

// Application model for the settle dungeon use case.
struct SettleDungeonRequest {
    std::int64_t player_id = 0;
    std::string battle_id;
    int dungeon_id = 0;
    int star = 0;
    bool result = true;
};

// Application result for the settle dungeon use case.
struct SettleDungeonResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    bool first_clear = false;
    std::vector<common::model::Reward> rewards;
};

// Application service that coordinates dungeon rules, locking and persistence boundaries.
class DungeonService {
public:
    DungeonService(PlayerLockRepository& player_lock_repository,
                   PlayerSnapshotPort& player_snapshot_port,
                   DungeonConfigRepository& dungeon_config_repository,
                   DungeonRepository& dungeon_repository,
                   BattleContextRepository& battle_context_repository);

    [[nodiscard]] EnterDungeonResponse EnterDungeon(const EnterDungeonRequest& request);
    [[nodiscard]] SettleDungeonResponse SettleDungeon(const SettleDungeonRequest& request);

private:
    [[nodiscard]] std::optional<DungeonConfig> LoadDungeonConfig(int dungeon_id) const;
    [[nodiscard]] std::optional<PlayerSnapshot> LoadPlayerSnapshot(std::int64_t player_id) const;
    [[nodiscard]] std::optional<common::model::BattleContext> LoadBattleContext(const std::string& battle_id) const;
    [[nodiscard]] std::optional<EnterDungeonResponse> ValidateEnterRequirements(
        const PlayerSnapshot& player_snapshot,
        const DungeonConfig& dungeon_config) const;
    [[nodiscard]] std::optional<SettleDungeonResponse> ValidateSettleInput(
        const SettleDungeonRequest& request,
        const DungeonConfig& dungeon_config) const;
    [[nodiscard]] std::optional<SettleDungeonResponse> ValidateBattleContext(
        const SettleDungeonRequest& request,
        const common::model::BattleContext& battle_context) const;
    [[nodiscard]] common::error::ErrorCode MapEnterStorageError(DungeonRepositoryError error) const;
    [[nodiscard]] common::error::ErrorCode MapSettleStorageError(DungeonRepositoryError error) const;
    [[nodiscard]] std::string GenerateBattleId(std::int64_t player_id, int dungeon_id);
    [[nodiscard]] bool AcquirePlayerLock(std::int64_t player_id);
    void ReleasePlayerLock(std::int64_t player_id);

    PlayerLockRepository& player_lock_repository_;
    PlayerSnapshotPort& player_snapshot_port_;
    DungeonConfigRepository& dungeon_config_repository_;
    DungeonRepository& dungeon_repository_;
    BattleContextRepository& battle_context_repository_;
    std::atomic<std::uint64_t> sequence_{1};
};

}  // namespace dungeon_server::dungeon
