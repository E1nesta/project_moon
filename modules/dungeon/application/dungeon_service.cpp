#include "modules/dungeon/application/dungeon_service.h"

#include <chrono>
#include <functional>

namespace dungeon_server::dungeon {

namespace {

EnterBattleResponse BuildEnterError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), 0, 0, 0};
}

SettleBattleResponse BuildSettleError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), 0, 0, {}};
}

RewardGrantStatusResponse BuildGrantStatusError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), 0, 0, {}};
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

std::string EntryIdempotencyKey(std::int64_t player_id, std::int64_t session_id) {
    return "battle-enter:" + std::to_string(player_id) + ":" + std::to_string(session_id);
}

std::string SettleIdempotencyKey(std::int64_t player_id, std::int64_t session_id) {
    return "battle-settle:" + std::to_string(player_id) + ":" + std::to_string(session_id);
}

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
      battle_context_repository_(battle_context_repository),
      id_generator_(1) {}

EnterBattleResponse DungeonService::EnterBattle(const EnterBattleRequest& request, const std::string& trace_id) {
    const auto config = LoadDungeonConfig(request.stage_id);
    if (!config.has_value()) {
        return BuildEnterError(common::error::ErrorCode::kDungeonNotFound, "stage config not found");
    }

    ScopedPlayerLock player_lock{
        [this, player_id = request.player_id] { ReleasePlayerLock(player_id); },
        AcquirePlayerLock(request.player_id)};
    if (!player_lock.acquired) {
        return BuildEnterError(common::error::ErrorCode::kPlayerBusy, "player is busy");
    }

    if (const auto unfinished = dungeon_repository_.FindUnsettledBattleByPlayerId(request.player_id); unfinished.has_value()) {
        return BuildEnterError(common::error::ErrorCode::kPlayerBusy, "unfinished battle exists");
    }

    const auto snapshot = LoadPlayerSnapshot(request.player_id);
    if (!snapshot.has_value()) {
        return BuildEnterError(common::error::ErrorCode::kPlayerNotFound, "player not found");
    }
    if (const auto validation = ValidateEnterRequirements(*snapshot, *config); validation.has_value()) {
        return *validation;
    }

    const auto session_id = id_generator_.Next();
    const auto seed = id_generator_.Next();
    const auto entry_idempotency_key = EntryIdempotencyKey(request.player_id, session_id);
    const auto prepare = player_snapshot_port_.PrepareBattleEntry(
        request.player_id, session_id, config->cost_stamina, entry_idempotency_key);
    if (!prepare.success) {
        return BuildEnterError(prepare.error_code, prepare.error_message);
    }

    const auto create_result = dungeon_repository_.CreateBattleSession(session_id,
                                                                       request.player_id,
                                                                       request.stage_id,
                                                                       request.mode,
                                                                       config->cost_stamina,
                                                                       prepare.remain_energy,
                                                                       snapshot->role_summaries,
                                                                       seed,
                                                                       entry_idempotency_key,
                                                                       trace_id);
    if (!create_result.success) {
        player_snapshot_port_.CancelBattleEntry(request.player_id,
                                               session_id,
                                               config->cost_stamina,
                                               "battle-cancel:" + entry_idempotency_key);
        return BuildEnterError(MapEnterStorageError(create_result.error), create_result.error_message);
    }

    battle_context_repository_.Save(create_result.battle_context);
    return {true, common::error::ErrorCode::kOk, "", session_id, prepare.remain_energy, seed};
}

SettleBattleResponse DungeonService::SettleBattle(const SettleBattleRequest& request, const std::string& trace_id) {
    const auto config = LoadDungeonConfig(request.stage_id);
    if (!config.has_value()) {
        return BuildSettleError(common::error::ErrorCode::kDungeonNotFound, "stage config not found");
    }
    if (const auto validation = ValidateSettleInput(request, *config); validation.has_value()) {
        return *validation;
    }

    ScopedPlayerLock player_lock{
        [this, player_id = request.player_id] { ReleasePlayerLock(player_id); },
        AcquirePlayerLock(request.player_id)};
    if (!player_lock.acquired) {
        return BuildSettleError(common::error::ErrorCode::kPlayerBusy, "player is busy");
    }

    const auto battle_context = LoadBattleContext(request.session_id);
    if (!battle_context.has_value()) {
        return BuildSettleError(common::error::ErrorCode::kBattleNotFound, "battle not found");
    }
    if (const auto validation = ValidateBattleContext(request, *battle_context); validation.has_value()) {
        return *validation;
    }
    if (battle_context->settled) {
        const auto status = dungeon_repository_.GetRewardGrantStatus(battle_context->reward_grant_id);
        if (!status.success) {
            return BuildSettleError(common::error::ErrorCode::kBattleAlreadySettled, "battle already settled");
        }
        return {true,
                common::error::ErrorCode::kOk,
                "",
                battle_context->reward_grant_id,
                status.grant_status,
                status.rewards};
    }

    std::vector<common::model::Reward> rewards = {{"gold", config->normal_gold_reward}};
    if (request.star >= config->max_star) {
        rewards.push_back({"diamond", config->first_clear_diamond_reward});
    }

    const auto reward_grant_id = id_generator_.Next();
    const auto settle_result = dungeon_repository_.RecordBattleSettlement(request.session_id,
                                                                          request.player_id,
                                                                          request.stage_id,
                                                                          request.result_code,
                                                                          request.star,
                                                                          request.client_score,
                                                                          reward_grant_id,
                                                                          rewards,
                                                                          SettleIdempotencyKey(request.player_id, request.session_id),
                                                                          trace_id);
    if (!settle_result.success) {
        return BuildSettleError(MapSettleStorageError(settle_result.error), settle_result.error_message);
    }

    battle_context_repository_.Delete(request.session_id);
    return {true, common::error::ErrorCode::kOk, "", reward_grant_id, 0, rewards};
}

RewardGrantStatusResponse DungeonService::GetRewardGrantStatus(std::int64_t reward_grant_id) const {
    const auto result = dungeon_repository_.GetRewardGrantStatus(reward_grant_id);
    if (!result.success) {
        return BuildGrantStatusError(common::error::ErrorCode::kBattleNotFound, result.error_message);
    }
    return {true, common::error::ErrorCode::kOk, "", reward_grant_id, result.grant_status, result.rewards};
}

std::optional<DungeonConfig> DungeonService::LoadDungeonConfig(int dungeon_id) const {
    return dungeon_config_repository_.FindByDungeonId(dungeon_id);
}

std::optional<PlayerSnapshot> DungeonService::LoadPlayerSnapshot(std::int64_t player_id) const {
    return player_snapshot_port_.GetBattleEntrySnapshot(player_id);
}

std::optional<common::model::BattleContext> DungeonService::LoadBattleContext(std::int64_t session_id) const {
    if (auto battle_context = dungeon_repository_.FindBattleById(session_id); battle_context.has_value()) {
        return battle_context;
    }
    return battle_context_repository_.FindByBattleId(session_id);
}

std::optional<EnterBattleResponse> DungeonService::ValidateEnterRequirements(const PlayerSnapshot& player_snapshot,
                                                                             const DungeonConfig& dungeon_config) const {
    if (player_snapshot.level < dungeon_config.required_level) {
        return BuildEnterError(common::error::ErrorCode::kDungeonLocked, "player level not enough");
    }
    if (player_snapshot.stamina < dungeon_config.cost_stamina) {
        return BuildEnterError(common::error::ErrorCode::kStaminaNotEnough, "stamina not enough");
    }
    return std::nullopt;
}

std::optional<SettleBattleResponse> DungeonService::ValidateSettleInput(const SettleBattleRequest& request,
                                                                        const DungeonConfig& dungeon_config) const {
    if (request.star < 0 || request.star > dungeon_config.max_star) {
        return BuildSettleError(common::error::ErrorCode::kInvalidStar, "star is out of range");
    }
    return std::nullopt;
}

std::optional<SettleBattleResponse> DungeonService::ValidateBattleContext(const SettleBattleRequest& request,
                                                                          const common::model::BattleContext& battle_context) const {
    if (battle_context.player_id != request.player_id || battle_context.stage_id != request.stage_id) {
        return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "battle context mismatch");
    }
    return std::nullopt;
}

common::error::ErrorCode DungeonService::MapEnterStorageError(DungeonRepositoryError error) const {
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

bool DungeonService::AcquirePlayerLock(std::int64_t player_id) {
    return player_lock_repository_.Acquire(player_id);
}

void DungeonService::ReleasePlayerLock(std::int64_t player_id) {
    player_lock_repository_.Release(player_id);
}

}  // namespace dungeon_server::dungeon
