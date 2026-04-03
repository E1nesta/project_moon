#include "modules/dungeon/application/dungeon_service.h"

#include <functional>
#include <sstream>

namespace dungeon_server::dungeon {

namespace {

EnterDungeonResponse BuildEnterError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), "", 0};
}

EnterDungeonResponse BuildEnterSuccess(std::string battle_id, int remain_stamina) {
    return {true, common::error::ErrorCode::kOk, "", std::move(battle_id), remain_stamina};
}

SettleDungeonResponse BuildSettleError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), false, {}};
}

SettleDungeonResponse BuildSettleSuccess(bool first_clear, std::vector<common::model::Reward> rewards) {
    return {true, common::error::ErrorCode::kOk, "", first_clear, std::move(rewards)};
}

struct ScopedPlayerLock {
    std::function<void()> release;
    bool acquired = false;

    ~ScopedPlayerLock() {
        if (acquired && release) {
            release();
        }
    }
};

}  // namespace

DungeonService::DungeonService(PlayerLockRepository& player_lock_repository,
                               PlayerSnapshotPort& player_snapshot_port,
                               DungeonConfigRepository& dungeon_config_repository,
                               DungeonRepository& dungeon_repository,
                               BattleContextRepository& battle_context_repository)
    : player_lock_repository_(player_lock_repository),
      player_snapshot_port_(player_snapshot_port),
      dungeon_config_repository_(dungeon_config_repository),
      dungeon_repository_(dungeon_repository),
      battle_context_repository_(battle_context_repository) {}

EnterDungeonResponse DungeonService::EnterDungeon(const EnterDungeonRequest& request) {
    const auto dungeon_config = LoadDungeonConfig(request.dungeon_id);
    if (!dungeon_config.has_value()) {
        return BuildEnterError(common::error::ErrorCode::kDungeonNotFound, "dungeon config not found");
    }

    ScopedPlayerLock player_lock{
        [this, player_id = request.player_id] { ReleasePlayerLock(player_id); },
        AcquirePlayerLock(request.player_id)};
    if (!player_lock.acquired) {
        return BuildEnterError(common::error::ErrorCode::kPlayerBusy, "player is busy");
    }

    const auto player_snapshot = LoadPlayerSnapshot(request.player_id);
    if (!player_snapshot.has_value()) {
        return BuildEnterError(common::error::ErrorCode::kPlayerNotFound, "player not found");
    }

    if (const auto validation = ValidateEnterRequirements(*player_snapshot, *dungeon_config); validation.has_value()) {
        return *validation;
    }

    const auto battle_id = GenerateBattleId(request.player_id, request.dungeon_id);
    const auto enter_result = dungeon_repository_.EnterDungeon(*player_snapshot, *dungeon_config, battle_id);
    if (!enter_result.success) {
        return BuildEnterError(MapEnterStorageError(enter_result.error), enter_result.error_message);
    }

    battle_context_repository_.Save(enter_result.battle_context);
    player_snapshot_port_.InvalidatePlayerSnapshot(request.player_id);
    return BuildEnterSuccess(std::move(battle_id), enter_result.remain_stamina);
}

SettleDungeonResponse DungeonService::SettleDungeon(const SettleDungeonRequest& request) {
    if (!request.result) {
        return BuildSettleError(common::error::ErrorCode::kStorageError, "only success settlement is supported in demo");
    }

    const auto dungeon_config = LoadDungeonConfig(request.dungeon_id);
    if (!dungeon_config.has_value()) {
        return BuildSettleError(common::error::ErrorCode::kDungeonNotFound, "dungeon config not found");
    }

    if (const auto validation = ValidateSettleInput(request, *dungeon_config); validation.has_value()) {
        return *validation;
    }

    ScopedPlayerLock player_lock{
        [this, player_id = request.player_id] { ReleasePlayerLock(player_id); },
        AcquirePlayerLock(request.player_id)};
    if (!player_lock.acquired) {
        return BuildSettleError(common::error::ErrorCode::kPlayerBusy, "player is busy");
    }

    const auto battle_context = LoadBattleContext(request.battle_id);
    if (!battle_context.has_value()) {
        return BuildSettleError(common::error::ErrorCode::kBattleNotFound, "battle not found");
    }

    if (const auto validation = ValidateBattleContext(request, *battle_context); validation.has_value()) {
        return *validation;
    }

    const auto settle_result = dungeon_repository_.SettleDungeon(*battle_context, *dungeon_config, request.star);
    if (!settle_result.success) {
        return BuildSettleError(MapSettleStorageError(settle_result.error), settle_result.error_message);
    }

    battle_context_repository_.Delete(request.battle_id);
    player_snapshot_port_.InvalidatePlayerSnapshot(request.player_id);
    return BuildSettleSuccess(settle_result.first_clear, std::move(settle_result.rewards));
}

std::optional<DungeonConfig> DungeonService::LoadDungeonConfig(int dungeon_id) const {
    return dungeon_config_repository_.FindByDungeonId(dungeon_id);
}

std::optional<PlayerSnapshot> DungeonService::LoadPlayerSnapshot(std::int64_t player_id) const {
    return player_snapshot_port_.LoadPlayerSnapshot(player_id);
}

std::optional<common::model::BattleContext> DungeonService::LoadBattleContext(const std::string& battle_id) const {
    if (auto battle_context = battle_context_repository_.FindByBattleId(battle_id); battle_context.has_value()) {
        return battle_context;
    }
    return dungeon_repository_.FindBattleById(battle_id);
}

std::optional<EnterDungeonResponse> DungeonService::ValidateEnterRequirements(
    const PlayerSnapshot& player_snapshot,
    const DungeonConfig& dungeon_config) const {
    if (player_snapshot.level < dungeon_config.required_level) {
        return BuildEnterError(common::error::ErrorCode::kDungeonLocked, "player level not enough");
    }

    if (player_snapshot.stamina < dungeon_config.cost_stamina) {
        return BuildEnterError(common::error::ErrorCode::kStaminaNotEnough, "stamina not enough");
    }

    return std::nullopt;
}

std::optional<SettleDungeonResponse> DungeonService::ValidateSettleInput(
    const SettleDungeonRequest& request,
    const DungeonConfig& dungeon_config) const {
    if (request.star <= 0 || request.star > dungeon_config.max_star) {
        return BuildSettleError(common::error::ErrorCode::kInvalidStar, "star is out of range");
    }

    return std::nullopt;
}

std::optional<SettleDungeonResponse> DungeonService::ValidateBattleContext(
    const SettleDungeonRequest& request,
    const common::model::BattleContext& battle_context) const {
    if (battle_context.player_id != request.player_id || battle_context.dungeon_id != request.dungeon_id) {
        return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "battle context mismatch");
    }

    if (battle_context.settled) {
        return BuildSettleError(common::error::ErrorCode::kBattleAlreadySettled, "battle already settled");
    }

    return std::nullopt;
}

common::error::ErrorCode DungeonService::MapEnterStorageError(DungeonRepositoryError error) const {
    if (error == DungeonRepositoryError::kStaminaNotEnough) {
        return common::error::ErrorCode::kStaminaNotEnough;
    }
    if (error == DungeonRepositoryError::kUnfinishedBattleExists) {
        return common::error::ErrorCode::kPlayerBusy;
    }
    return common::error::ErrorCode::kStorageError;
}

common::error::ErrorCode DungeonService::MapSettleStorageError(DungeonRepositoryError error) const {
    if (error == DungeonRepositoryError::kBattleAlreadySettled) {
        return common::error::ErrorCode::kBattleAlreadySettled;
    }
    return common::error::ErrorCode::kStorageError;
}

std::string DungeonService::GenerateBattleId(std::int64_t player_id, int dungeon_id) {
    std::ostringstream output;
    output << "battle-" << player_id << '-' << dungeon_id << '-' << sequence_.fetch_add(1);
    return output.str();
}

bool DungeonService::AcquirePlayerLock(std::int64_t player_id) {
    return player_lock_repository_.Acquire(player_id);
}

void DungeonService::ReleasePlayerLock(std::int64_t player_id) {
    player_lock_repository_.Release(player_id);
}

}  // namespace dungeon_server::dungeon
