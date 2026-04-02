#pragma once

#include "common/error/error_code.h"
#include "common/model/battle_context.h"
#include "common/model/reward.h"
#include "dungeon_server/dungeon/battle_context_repository.h"
#include "dungeon_server/dungeon/dungeon_config_repository.h"
#include "dungeon_server/dungeon/mysql_dungeon_repository.h"
#include "dungeon_server/dungeon/player_lock_repository.h"
#include "game_server/player/player_cache_repository.h"
#include "game_server/player/player_repository.h"
#include "login_server/session/session_repository.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace dungeon_server::dungeon {

// Application model for the enter dungeon use case.
struct EnterDungeonRequest {
    std::string session_id;
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
    std::string session_id;
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
    DungeonService(login_server::session::SessionRepository& session_repository,
                   PlayerLockRepository& player_lock_repository,
                   game_server::player::PlayerRepository& player_repository,
                   game_server::player::PlayerCacheRepository& player_cache_repository,
                   DungeonConfigRepository& dungeon_config_repository,
                   MySqlDungeonRepository& dungeon_repository,
                   BattleContextRepository& battle_context_repository);

    [[nodiscard]] EnterDungeonResponse EnterDungeon(const EnterDungeonRequest& request);
    [[nodiscard]] SettleDungeonResponse SettleDungeon(const SettleDungeonRequest& request);

private:
    [[nodiscard]] bool HasValidSession(const std::string& session_id, std::int64_t player_id) const;
    [[nodiscard]] std::optional<DungeonConfig> LoadDungeonConfig(int dungeon_id) const;
    [[nodiscard]] std::optional<common::model::PlayerState> LoadPlayerState(std::int64_t player_id) const;
    [[nodiscard]] std::optional<common::model::BattleContext> LoadBattleContext(const std::string& battle_id) const;
    [[nodiscard]] std::optional<EnterDungeonResponse> ValidateEnterRequirements(
        const common::model::PlayerState& player_state,
        const DungeonConfig& dungeon_config) const;
    [[nodiscard]] std::optional<SettleDungeonResponse> ValidateSettleInput(
        const SettleDungeonRequest& request,
        const DungeonConfig& dungeon_config) const;
    [[nodiscard]] std::optional<SettleDungeonResponse> ValidateBattleContext(
        const SettleDungeonRequest& request,
        const common::model::BattleContext& battle_context) const;
    [[nodiscard]] common::error::ErrorCode MapEnterStorageError(const std::string& error_message) const;
    [[nodiscard]] common::error::ErrorCode MapSettleStorageError(const std::string& error_message) const;
    [[nodiscard]] std::string GenerateBattleId(std::int64_t player_id, int dungeon_id);
    [[nodiscard]] bool AcquirePlayerLock(std::int64_t player_id);
    void ReleasePlayerLock(std::int64_t player_id);

    login_server::session::SessionRepository& session_repository_;
    PlayerLockRepository& player_lock_repository_;
    game_server::player::PlayerRepository& player_repository_;
    game_server::player::PlayerCacheRepository& player_cache_repository_;
    DungeonConfigRepository& dungeon_config_repository_;
    MySqlDungeonRepository& dungeon_repository_;
    BattleContextRepository& battle_context_repository_;
    std::atomic<std::uint64_t> sequence_{1};
};

}  // namespace dungeon_server::dungeon
