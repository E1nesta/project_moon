#include "modules/player/application/player_service.h"

#include "runtime/foundation/log/logger.h"

namespace game_server::player {

namespace {

LoadPlayerResponse BuildLoadPlayerError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), {}, false};
}

LoadPlayerResponse BuildLoadPlayerSuccess(const common::model::PlayerState& player_state, bool loaded_from_cache) {
    return {true, common::error::ErrorCode::kOk, "", player_state, loaded_from_cache};
}

PlayerSnapshotResponse BuildSnapshotSuccess(const common::model::PlayerState& player_state) {
    return {true,
            common::error::ErrorCode::kOk,
            "",
            true,
            player_state.profile.player_id,
            player_state.profile.level,
            player_state.profile.stamina};
}

BattleEntrySnapshotResponse BuildBattleEntrySnapshotSuccess(std::int64_t player_id,
                                                            int level,
                                                            int energy,
                                                            std::vector<common::model::PlayerRoleSummary> role_summaries) {
    return {true, common::error::ErrorCode::kOk, "", true, player_id, level, energy, std::move(role_summaries)};
}

BattleEntrySnapshotResponse BuildBattleEntrySnapshotMissing() {
    return {true, common::error::ErrorCode::kOk, "", false, 0, 0, 0, {}};
}

PlayerSnapshotResponse BuildSnapshotMissing() {
    return {true, common::error::ErrorCode::kOk, "", false, 0, 0, 0};
}

InvalidatePlayerCacheResponse BuildInvalidateSuccess() {
    return {true, common::error::ErrorCode::kOk, ""};
}

InvalidatePlayerCacheResponse BuildInvalidateFailure(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message)};
}

common::error::ErrorCode MapMutationError(PlayerMutationError error) {
    switch (error) {
    case PlayerMutationError::kPlayerNotFound:
        return common::error::ErrorCode::kPlayerNotFound;
    case PlayerMutationError::kStaminaNotEnough:
        return common::error::ErrorCode::kStaminaNotEnough;
    case PlayerMutationError::kBattleMismatch:
        return common::error::ErrorCode::kBattleMismatch;
    case PlayerMutationError::kAlreadyApplied:
        return common::error::ErrorCode::kBattleAlreadySettled;
    case PlayerMutationError::kStorageFailure:
        return common::error::ErrorCode::kStorageError;
    case PlayerMutationError::kNone:
        break;
    }
    return common::error::ErrorCode::kOk;
}

}  // namespace

PlayerService::PlayerService(PlayerRepository& player_repository,
                             PlayerCacheRepository& player_cache_repository)
    : player_repository_(player_repository),
      player_cache_repository_(player_cache_repository) {}

LoadPlayerResponse PlayerService::LoadPlayer(std::int64_t player_id) {
    if (const auto cached_state = LoadCachedPlayer(player_id); cached_state.has_value()) {
        return BuildLoadSuccess(*cached_state, true);
    }

    const auto player_state = LoadPlayerFromStorage(player_id);
    if (!player_state.has_value()) {
        return BuildLoadPlayerError(common::error::ErrorCode::kPlayerNotFound, "player not found");
    }

    player_cache_repository_.Save(*player_state);
    return BuildLoadSuccess(*player_state, false);
}

PlayerSnapshotResponse PlayerService::GetPlayerSnapshot(std::int64_t player_id) {
    if (const auto cached_state = LoadCachedPlayer(player_id); cached_state.has_value()) {
        return BuildSnapshotSuccess(*cached_state);
    }

    const auto player_state = LoadPlayerFromStorage(player_id);
    if (!player_state.has_value()) {
        return BuildSnapshotMissing();
    }

    player_cache_repository_.Save(*player_state);
    return BuildSnapshotSuccess(*player_state);
}

BattleEntrySnapshotResponse PlayerService::GetBattleEntrySnapshot(std::int64_t player_id) {
    const auto result = player_repository_.GetBattleEntrySnapshot(player_id);
    if (!result.success) {
        return {false, MapMutationError(result.error), result.error_message, false, 0, 0, 0, {}};
    }
    if (!result.found) {
        return BuildBattleEntrySnapshotMissing();
    }
    return BuildBattleEntrySnapshotSuccess(player_id, result.level, result.energy, std::move(result.role_summaries));
}

InvalidatePlayerCacheResponse PlayerService::InvalidatePlayerCache(std::int64_t player_id) {
    if (player_cache_repository_.Invalidate(player_id)) {
        return BuildInvalidateSuccess();
    }

    return BuildInvalidateFailure(common::error::ErrorCode::kStorageError, "failed to invalidate player cache");
}

PrepareBattleEntryResponse PlayerService::PrepareBattleEntry(std::int64_t player_id,
                                                             std::int64_t session_id,
                                                             int energy_cost,
                                                             const std::string& idempotency_key) {
    const auto result = player_repository_.PrepareBattleEntry(player_id, session_id, energy_cost, idempotency_key);
    if (!result.success) {
        return {false, MapMutationError(result.error), result.error_message, 0};
    }

    InvalidatePlayerCacheBestEffort(player_id);
    return {true, common::error::ErrorCode::kOk, "", result.remain_energy};
}

CancelBattleEntryResponse PlayerService::CancelBattleEntry(std::int64_t player_id,
                                                           std::int64_t session_id,
                                                           int energy_refund,
                                                           const std::string& idempotency_key) {
    const auto result = player_repository_.CancelBattleEntry(player_id, session_id, energy_refund, idempotency_key);
    if (!result.success) {
        return {false, MapMutationError(result.error), result.error_message};
    }

    InvalidatePlayerCacheBestEffort(player_id);
    return {true, common::error::ErrorCode::kOk, ""};
}

ApplyRewardGrantResponse PlayerService::ApplyRewardGrant(std::int64_t player_id,
                                                         std::int64_t grant_id,
                                                         std::int64_t session_id,
                                                         const std::vector<common::model::Reward>& rewards,
                                                         const std::string& idempotency_key) {
    const auto result = player_repository_.ApplyRewardGrant(player_id, grant_id, session_id, rewards, idempotency_key);
    if (!result.success) {
        return {false, MapMutationError(result.error), result.error_message, {}};
    }

    InvalidatePlayerCacheBestEffort(player_id);
    return {true, common::error::ErrorCode::kOk, "", result.applied_currencies};
}

std::optional<common::model::PlayerState> PlayerService::LoadCachedPlayer(std::int64_t player_id) const {
    return player_cache_repository_.FindByPlayerId(player_id);
}

std::optional<common::model::PlayerState> PlayerService::LoadPlayerFromStorage(std::int64_t player_id) const {
    return player_repository_.LoadPlayerState(player_id);
}

LoadPlayerResponse PlayerService::BuildLoadSuccess(const common::model::PlayerState& player_state,
                                                   bool loaded_from_cache) const {
    return BuildLoadPlayerSuccess(player_state, loaded_from_cache);
}

void PlayerService::InvalidatePlayerCacheBestEffort(std::int64_t player_id) const {
    if (player_cache_repository_.Invalidate(player_id)) {
        return;
    }

    common::log::Logger::Instance().Log(
        common::log::LogLevel::kWarn,
        "player cache invalidation failed after mutation for player_id=" + std::to_string(player_id));
}

}  // namespace game_server::player
