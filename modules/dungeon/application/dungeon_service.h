#pragma once

#include "runtime/foundation/error/error_code.h"
#include "runtime/foundation/id/id_generator.h"
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
struct EnterBattleRequest {
    std::int64_t player_id = 0;
    int stage_id = 0;
    std::string mode = "pve";
};

struct EnterBattleResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    std::int64_t session_id = 0;
    int remain_stamina = 0;
    std::int64_t seed = 0;
};

struct SettleBattleRequest {
    std::int64_t player_id = 0;
    std::int64_t session_id = 0;
    int stage_id = 0;
    int star = 0;
    int result_code = 1;
    std::int64_t client_score = 0;
};

struct SettleBattleResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    std::int64_t reward_grant_id = 0;
    int grant_status = 0;
    std::vector<common::model::Reward> reward_preview;
};

struct RewardGrantStatusResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    std::int64_t reward_grant_id = 0;
    int grant_status = 0;
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

    [[nodiscard]] EnterBattleResponse EnterBattle(const EnterBattleRequest& request, const std::string& trace_id);
    [[nodiscard]] SettleBattleResponse SettleBattle(const SettleBattleRequest& request, const std::string& trace_id);
    [[nodiscard]] RewardGrantStatusResponse GetRewardGrantStatus(std::int64_t reward_grant_id) const;

private:
    [[nodiscard]] std::optional<DungeonConfig> LoadDungeonConfig(int dungeon_id) const;
    [[nodiscard]] std::optional<PlayerSnapshot> LoadPlayerSnapshot(std::int64_t player_id) const;
    [[nodiscard]] std::optional<common::model::BattleContext> LoadBattleContext(std::int64_t session_id) const;
    [[nodiscard]] std::optional<EnterBattleResponse> ValidateEnterRequirements(
        const PlayerSnapshot& player_snapshot,
        const DungeonConfig& dungeon_config) const;
    [[nodiscard]] std::optional<SettleBattleResponse> ValidateSettleInput(
        const SettleBattleRequest& request,
        const DungeonConfig& dungeon_config) const;
    [[nodiscard]] std::optional<SettleBattleResponse> ValidateBattleContext(
        const SettleBattleRequest& request,
        const common::model::BattleContext& battle_context) const;
    [[nodiscard]] common::error::ErrorCode MapEnterStorageError(DungeonRepositoryError error) const;
    [[nodiscard]] common::error::ErrorCode MapSettleStorageError(DungeonRepositoryError error) const;
    [[nodiscard]] bool AcquirePlayerLock(std::int64_t player_id);
    void ReleasePlayerLock(std::int64_t player_id);

    PlayerLockRepository& player_lock_repository_;
    PlayerSnapshotPort& player_snapshot_port_;
    DungeonConfigRepository& dungeon_config_repository_;
    DungeonRepository& dungeon_repository_;
    BattleContextRepository& battle_context_repository_;
    common::id::IdGenerator id_generator_;
};

}  // namespace dungeon_server::dungeon
