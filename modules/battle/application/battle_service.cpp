#include "modules/battle/application/battle_service.h"

#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>

namespace battle_server::battle {

namespace {

constexpr int kBattleResultLose = 0;
constexpr int kBattleResultWin = 1;
constexpr int kBattleEntryRollbackAttempts = 3;

EnterBattleResponse BuildEnterError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), 0, 0, 0, ""};
}

SettleBattleResponse BuildSettleError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), 0, 0, {}};
}

RewardGrantStatusResponse BuildGrantStatusError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), 0, 0, {}};
}

ActiveBattleResponse BuildActiveBattleError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), false, 0, 0, "", 0, 0, ""};
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

std::string EntryIdempotencyKey(std::int64_t player_id, std::uint64_t request_id) {
    return "battle-enter:" + std::to_string(player_id) + ":" + std::to_string(request_id);
}

std::string SettleIdempotencyKey(std::int64_t player_id, std::int64_t session_id) {
    return "battle-settle:" + std::to_string(player_id) + ":" + std::to_string(session_id);
}

std::uint64_t Fnv1a64(std::string_view value) {
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    for (const auto ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= kPrime;
    }
    return hash;
}

}  // namespace

BattleService::BattleService(PlayerLockRepository& player_lock_repository,
                               PlayerSnapshotPort& player_snapshot_port,
                               StageConfigRepository& stage_config_repository,
                               BattleRepository& battle_repository,
                               BattleContextRepository& battle_context_repository,
                               std::uint16_t id_generator_node_id,
                               std::string settle_token_secret)
    : player_lock_repository_(player_lock_repository),
      player_snapshot_port_(player_snapshot_port),
      stage_config_repository_(stage_config_repository),
      battle_repository_(battle_repository),
      battle_context_repository_(battle_context_repository),
      id_generator_(id_generator_node_id),
      settle_token_secret_(std::move(settle_token_secret)) {}

EnterBattleResponse BattleService::EnterBattle(const EnterBattleRequest& request, const std::string& trace_id) {
    if (request.request_id == 0) {
        return BuildEnterError(common::error::ErrorCode::kRequestContextInvalid, "request_id is invalid");
    }
    const auto config = LoadStageConfig(request.stage_id);
    if (!config.has_value()) {
        return BuildEnterError(common::error::ErrorCode::kStageNotFound, "stage config not found");
    }

    ScopedPlayerLock player_lock{
        [this, player_id = request.player_id] { ReleasePlayerLock(player_id); },
        AcquirePlayerLock(request.player_id)};
    if (!player_lock.acquired) {
        return BuildEnterError(common::error::ErrorCode::kPlayerBusy, "player is busy");
    }

    const auto entry_idempotency_key = EntryIdempotencyKey(request.player_id, request.request_id);
    if (const auto replay = battle_repository_.FindUnsettledBattleByIdempotencyKey(entry_idempotency_key);
        replay.has_value()) {
        return {true,
                common::error::ErrorCode::kOk,
                "",
                replay->session_id,
                replay->remain_energy_after,
                replay->seed,
                BuildSettleToken(replay->player_id, replay->session_id, replay->stage_id, replay->seed)};
    }

    PlayerSnapshot snapshot_data;
    std::int64_t session_id = 0;
    std::int64_t seed = 0;
    if (const auto operation = battle_repository_.FindBattleEntryOperationByIdempotencyKey(entry_idempotency_key);
        operation.has_value()) {
        if (operation->player_id != request.player_id || operation->stage_id != request.stage_id || operation->mode != request.mode) {
            return BuildEnterError(common::error::ErrorCode::kBattleMismatch, "battle entry request mismatch");
        }
        if (operation->status == BattleEntryOperationStatus::kActive) {
            return {true,
                    common::error::ErrorCode::kOk,
                    "",
                    operation->session_id,
                    operation->remain_energy_after,
                    operation->seed,
                    BuildSettleToken(operation->player_id,
                                     operation->session_id,
                                     operation->stage_id,
                                     operation->seed)};
        }
        if (operation->status == BattleEntryOperationStatus::kRolledBack) {
            return BuildEnterError(common::error::ErrorCode::kStorageError, "battle entry operation already rolled back");
        }
        if (operation->status == BattleEntryOperationStatus::kCompleted) {
            return BuildEnterError(common::error::ErrorCode::kBattleAlreadySettled,
                                   "battle entry operation already completed");
        }
        if (operation->status == BattleEntryOperationStatus::kFailed) {
            return BuildEnterError(operation->error_code, operation->error_message);
        }
        const auto snapshot = LoadPlayerSnapshot(request.player_id);
        if (!snapshot.success) {
            return BuildEnterError(snapshot.error_code, snapshot.error_message);
        }
        if (!snapshot.found) {
            return BuildEnterError(common::error::ErrorCode::kPlayerNotFound, "player not found");
        }
        snapshot_data = snapshot.snapshot;
        session_id = operation->session_id;
        seed = operation->seed;
    } else {
        if (const auto unfinished = battle_repository_.FindUnsettledBattleByPlayerId(request.player_id);
            unfinished.has_value()) {
            return BuildEnterError(common::error::ErrorCode::kPlayerBusy, "unfinished battle exists");
        }

        const auto snapshot = LoadPlayerSnapshot(request.player_id);
        if (!snapshot.success) {
            return BuildEnterError(snapshot.error_code, snapshot.error_message);
        }
        if (!snapshot.found) {
            return BuildEnterError(common::error::ErrorCode::kPlayerNotFound, "player not found");
        }
        if (const auto validation = ValidateEnterRequirements(snapshot.snapshot, *config); validation.has_value()) {
            return *validation;
        }
        snapshot_data = snapshot.snapshot;

        session_id = id_generator_.Next();
        seed = id_generator_.Next();
        std::string create_operation_error;
        if (!battle_repository_.CreateBattleEntryOperation(
                request.player_id,
                request.stage_id,
                request.mode,
                session_id,
                seed,
                entry_idempotency_key,
                &create_operation_error)) {
            return BuildEnterError(common::error::ErrorCode::kStorageError,
                                   "failed to create battle entry operation: " + create_operation_error);
        }
    }

    const auto prepare = player_snapshot_port_.PrepareBattleEntry(
        request.player_id, session_id, config->cost_stamina, entry_idempotency_key);
    if (!prepare.success) {
        if (prepare.error_code != common::error::ErrorCode::kStorageError &&
            prepare.error_code != common::error::ErrorCode::kServiceUnavailable) {
            std::string mark_failed_error;
            if (!battle_repository_.MarkBattleEntryOperationFailed(
                    entry_idempotency_key, prepare.error_code, prepare.error_message, &mark_failed_error)) {
                return BuildEnterError(common::error::ErrorCode::kStorageError,
                                       "battle entry prepare failed and failure finalize failed: " +
                                           mark_failed_error);
            }
        }
        return BuildEnterError(prepare.error_code, prepare.error_message);
    }

    const auto create_result = battle_repository_.CreateBattleSession(session_id,
                                                                       request.player_id,
                                                                       request.stage_id,
                                                                       request.mode,
                                                                       config->cost_stamina,
                                                                       prepare.remain_energy,
                                                                       snapshot_data.role_summaries,
                                                                       seed,
                                                                       entry_idempotency_key,
                                                                       trace_id);
    if (!create_result.success) {
        std::string rollback_error_message;
        if (!RollbackBattleEntry(request.player_id,
                                 session_id,
                                 config->cost_stamina,
                                 prepare.remain_energy + config->cost_stamina,
                                 entry_idempotency_key,
                                 &rollback_error_message)) {
            return BuildEnterError(common::error::ErrorCode::kStorageError,
                                   "battle session creation failed and stamina rollback failed: " +
                                       rollback_error_message);
        }
        std::string mark_rolled_back_error;
        if (!battle_repository_.MarkBattleEntryOperationRolledBack(entry_idempotency_key, &mark_rolled_back_error)) {
            return BuildEnterError(common::error::ErrorCode::kStorageError,
                                   "battle session creation failed and rollback finalize failed: " +
                                       mark_rolled_back_error);
        }
        return BuildEnterError(MapEnterStorageError(create_result.error), create_result.error_message);
    }

    battle_context_repository_.Save(create_result.battle_context);
    return {true,
            common::error::ErrorCode::kOk,
            "",
            session_id,
            prepare.remain_energy,
            seed,
            BuildSettleToken(request.player_id, session_id, request.stage_id, seed)};
}

SettleBattleResponse BattleService::SettleBattle(const SettleBattleRequest& request, const std::string& trace_id) {
    (void)trace_id;
    const auto config = LoadStageConfig(request.stage_id);
    if (!config.has_value()) {
        return BuildSettleError(common::error::ErrorCode::kStageNotFound, "stage config not found");
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
        auto status = battle_repository_.GetRewardGrantStatus(request.player_id, battle_context->reward_grant_id);
        if (!status.success) {
            return BuildSettleError(common::error::ErrorCode::kBattleAlreadySettled, "battle already settled");
        }
        if (status.grant_status == 0) {
            const auto settle_idempotency_key = SettleIdempotencyKey(request.player_id, request.session_id);
            const auto apply_reward = player_snapshot_port_.ApplyRewardGrant(
                request.player_id, battle_context->reward_grant_id, request.session_id, status.rewards, settle_idempotency_key);
            if (!apply_reward.success) {
                return BuildSettleError(apply_reward.error_code, apply_reward.error_message);
            }

            const auto mark_granted = battle_repository_.MarkRewardGrantGranted(battle_context->reward_grant_id);
            if (!mark_granted.success) {
                return BuildSettleError(MapSettleStorageError(mark_granted.error), mark_granted.error_message);
            }

            battle_context_repository_.Delete(request.session_id);
            status.grant_status = 1;
        }
        return {true,
                common::error::ErrorCode::kOk,
                "",
                battle_context->reward_grant_id,
                status.grant_status,
                status.rewards};
    }

    // Current MVP only grants fixed clear reward on a successful result.
    // Client-reported star does not mint premium currency.
    std::vector<common::model::Reward> rewards;
    if (request.result_code == kBattleResultWin) {
        rewards.push_back({"gold", config->normal_gold_reward});
    }

    const auto reward_grant_id = request.session_id;
    const auto settle_idempotency_key = SettleIdempotencyKey(request.player_id, request.session_id);
    const auto settle_result = battle_repository_.RecordBattleSettlement(request.session_id,
                                                                          request.player_id,
                                                                          request.stage_id,
                                                                          request.result_code,
                                                                          request.star,
                                                                          request.client_score,
                                                                          reward_grant_id,
                                                                          rewards,
                                                                          settle_idempotency_key);
    if (!settle_result.success) {
        return BuildSettleError(MapSettleStorageError(settle_result.error), settle_result.error_message);
    }

    const auto apply_reward = player_snapshot_port_.ApplyRewardGrant(
        request.player_id, reward_grant_id, request.session_id, rewards, settle_idempotency_key);
    if (!apply_reward.success) {
        return BuildSettleError(apply_reward.error_code, apply_reward.error_message);
    }

    const auto mark_granted = battle_repository_.MarkRewardGrantGranted(reward_grant_id);
    if (!mark_granted.success) {
        return BuildSettleError(MapSettleStorageError(mark_granted.error), mark_granted.error_message);
    }

    battle_context_repository_.Delete(request.session_id);
    return {true, common::error::ErrorCode::kOk, "", reward_grant_id, 1, rewards};
}

ActiveBattleResponse BattleService::GetActiveBattle(std::int64_t player_id) const {
    if (player_id <= 0) {
        return BuildActiveBattleError(common::error::ErrorCode::kRequestContextInvalid, "player_id is invalid");
    }

    const auto battle_context = battle_repository_.FindUnsettledBattleByPlayerId(player_id);
    if (!battle_context.has_value()) {
        return {true, common::error::ErrorCode::kOk, "", false, 0, 0, "", 0, 0, ""};
    }

    return {true,
            common::error::ErrorCode::kOk,
            "",
            true,
            battle_context->session_id,
            battle_context->stage_id,
            battle_context->mode,
            battle_context->remain_energy_after,
            battle_context->seed,
            BuildSettleToken(
                battle_context->player_id, battle_context->session_id, battle_context->stage_id, battle_context->seed)};
}

RewardGrantStatusResponse BattleService::GetRewardGrantStatus(std::int64_t player_id,
                                                              std::int64_t reward_grant_id) const {
    const auto result = battle_repository_.GetRewardGrantStatus(player_id, reward_grant_id);
    if (!result.success) {
        return BuildGrantStatusError(common::error::ErrorCode::kBattleNotFound, result.error_message);
    }
    return {true, common::error::ErrorCode::kOk, "", reward_grant_id, result.grant_status, result.rewards};
}

std::optional<StageConfig> BattleService::LoadStageConfig(int stage_id) const {
    return stage_config_repository_.FindByStageId(stage_id);
}

GetBattleEntrySnapshotPortResponse BattleService::LoadPlayerSnapshot(std::int64_t player_id) const {
    return player_snapshot_port_.GetBattleEntrySnapshot(player_id);
}

std::optional<common::model::BattleContext> BattleService::LoadBattleContext(std::int64_t session_id) const {
    if (auto battle_context = battle_repository_.FindBattleById(session_id); battle_context.has_value()) {
        return battle_context;
    }
    return battle_context_repository_.FindByBattleId(session_id);
}

bool BattleService::RollbackBattleEntry(std::int64_t player_id,
                                        std::int64_t session_id,
                                        int energy_refund,
                                        int expected_stamina_after_rollback,
                                        const std::string& entry_idempotency_key,
                                        std::string* error_message) const {
    std::string last_error_message;
    for (int attempt = 0; attempt < kBattleEntryRollbackAttempts; ++attempt) {
        const auto cancel_result = player_snapshot_port_.CancelBattleEntry(
            player_id, session_id, energy_refund, "battle-cancel:" + entry_idempotency_key);
        if (cancel_result.success) {
            return true;
        }
        last_error_message = cancel_result.error_message;

        const auto snapshot = LoadPlayerSnapshot(player_id);
        if (snapshot.success && snapshot.found && snapshot.snapshot.stamina == expected_stamina_after_rollback) {
            return true;
        }
    }

    if (error_message != nullptr) {
        *error_message = last_error_message.empty() ? "rollback attempts exhausted" : last_error_message;
    }
    return false;
}

std::optional<EnterBattleResponse> BattleService::ValidateEnterRequirements(const PlayerSnapshot& player_snapshot,
                                                                             const StageConfig& stage_config) const {
    if (player_snapshot.level < stage_config.required_level) {
        return BuildEnterError(common::error::ErrorCode::kStageLocked, "player level not enough");
    }
    if (player_snapshot.stamina < stage_config.cost_stamina) {
        return BuildEnterError(common::error::ErrorCode::kStaminaNotEnough, "stamina not enough");
    }
    return std::nullopt;
}

std::optional<SettleBattleResponse> BattleService::ValidateSettleInput(const SettleBattleRequest& request,
                                                                        const StageConfig& stage_config) const {
    if (request.star < 0 || request.star > stage_config.max_star) {
        return BuildSettleError(common::error::ErrorCode::kInvalidStar, "star is out of range");
    }
    if (request.result_code != kBattleResultLose && request.result_code != kBattleResultWin) {
        return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "result_code is invalid");
    }
    if (request.result_code == kBattleResultWin) {
        if (request.star <= 0) {
            return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "win result requires star > 0");
        }
        if (request.client_score <= 0) {
            return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "win result requires positive client_score");
        }
    } else {
        if (request.star != 0) {
            return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "lose result requires star = 0");
        }
        if (request.client_score != 0) {
            return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "lose result requires client_score = 0");
        }
    }
    if (request.settle_token.empty()) {
        return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "settle token is missing");
    }
    return std::nullopt;
}

std::optional<SettleBattleResponse> BattleService::ValidateBattleContext(const SettleBattleRequest& request,
                                                                          const common::model::BattleContext& battle_context) const {
    if (battle_context.player_id != request.player_id || battle_context.stage_id != request.stage_id) {
        return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "battle context mismatch");
    }
    if (request.settle_token !=
        BuildSettleToken(battle_context.player_id, battle_context.session_id, battle_context.stage_id, battle_context.seed)) {
        return BuildSettleError(common::error::ErrorCode::kBattleMismatch, "settle token mismatch");
    }
    return std::nullopt;
}

std::string BattleService::BuildSettleToken(std::int64_t player_id,
                                            std::int64_t session_id,
                                            int stage_id,
                                            std::int64_t seed) const {
    std::ostringstream payload;
    payload << settle_token_secret_ << '|' << player_id << '|' << session_id << '|' << stage_id << '|' << seed;
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << Fnv1a64(payload.str());
    return output.str();
}

common::error::ErrorCode BattleService::MapEnterStorageError(BattleRepositoryError error) const {
    if (error == BattleRepositoryError::kUnfinishedBattleExists) {
        return common::error::ErrorCode::kPlayerBusy;
    }
    return common::error::ErrorCode::kStorageError;
}

common::error::ErrorCode BattleService::MapSettleStorageError(BattleRepositoryError error) const {
    if (error == BattleRepositoryError::kBattleAlreadySettled) {
        return common::error::ErrorCode::kBattleAlreadySettled;
    }
    if (error == BattleRepositoryError::kGrantNotFound) {
        return common::error::ErrorCode::kBattleNotFound;
    }
    return common::error::ErrorCode::kStorageError;
}

bool BattleService::AcquirePlayerLock(std::int64_t player_id) {
    return player_lock_repository_.Acquire(player_id);
}

void BattleService::ReleasePlayerLock(std::int64_t player_id) {
    player_lock_repository_.Release(player_id);
}

}  // namespace battle_server::battle
