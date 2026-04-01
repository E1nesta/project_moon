#include "dungeon_server/dungeon/dungeon_service.h"

#include <sstream>

namespace dungeon_server::dungeon {

DungeonService::DungeonService(login_server::session::SessionRepository& session_repository,
                               common::redis::RedisClient& redis_client,
                               game_server::player::PlayerRepository& player_repository,
                               game_server::player::PlayerCacheRepository& player_cache_repository,
                               DungeonConfigRepository& dungeon_config_repository,
                               MySqlDungeonRepository& dungeon_repository,
                               BattleContextRepository& battle_context_repository)
    : session_repository_(session_repository),
      redis_client_(redis_client),
      player_repository_(player_repository),
      player_cache_repository_(player_cache_repository),
      dungeon_config_repository_(dungeon_config_repository),
      dungeon_repository_(dungeon_repository),
      battle_context_repository_(battle_context_repository) {}

EnterDungeonResponse DungeonService::EnterDungeon(const EnterDungeonRequest& request) {
    const auto session = session_repository_.FindById(request.session_id);
    if (!session.has_value() || session->player_id != request.player_id) {
        return {false, common::error::ErrorCode::kSessionInvalid, "session invalid", "", 0};
    }

    const auto dungeon_config = dungeon_config_repository_.FindByDungeonId(request.dungeon_id);
    if (!dungeon_config.has_value()) {
        return {false, common::error::ErrorCode::kDungeonNotFound, "dungeon config not found", "", 0};
    }

    if (!AcquirePlayerLock(request.player_id)) {
        return {false, common::error::ErrorCode::kPlayerBusy, "player is busy", "", 0};
    }

    const auto player_state = player_repository_.LoadPlayerState(request.player_id);
    if (!player_state.has_value()) {
        ReleasePlayerLock(request.player_id);
        return {false, common::error::ErrorCode::kPlayerNotFound, "player not found", "", 0};
    }

    if (player_state->profile.level < dungeon_config->required_level) {
        ReleasePlayerLock(request.player_id);
        return {false, common::error::ErrorCode::kDungeonLocked, "player level not enough", "", 0};
    }

    if (player_state->profile.stamina < dungeon_config->cost_stamina) {
        ReleasePlayerLock(request.player_id);
        return {false, common::error::ErrorCode::kStaminaNotEnough, "stamina not enough", "", 0};
    }

    const auto battle_id = GenerateBattleId(request.player_id, request.dungeon_id);
    const auto enter_result = dungeon_repository_.EnterDungeon(*player_state, *dungeon_config, battle_id);
    if (!enter_result.success) {
        ReleasePlayerLock(request.player_id);
        const auto code = enter_result.error_message == "stamina not enough" ? common::error::ErrorCode::kStaminaNotEnough
                          : enter_result.error_message == "unfinished battle exists" ? common::error::ErrorCode::kPlayerBusy
                                                                              : common::error::ErrorCode::kStorageError;
        return {false, code, enter_result.error_message, "", 0};
    }

    battle_context_repository_.Save(enter_result.battle_context);
    player_cache_repository_.Invalidate(request.player_id);
    ReleasePlayerLock(request.player_id);
    return {true, common::error::ErrorCode::kOk, "", battle_id, enter_result.remain_stamina};
}

SettleDungeonResponse DungeonService::SettleDungeon(const SettleDungeonRequest& request) {
    if (!request.result) {
        return {false, common::error::ErrorCode::kStorageError, "only success settlement is supported in demo", false, {}};
    }

    const auto session = session_repository_.FindById(request.session_id);
    if (!session.has_value() || session->player_id != request.player_id) {
        return {false, common::error::ErrorCode::kSessionInvalid, "session invalid", false, {}};
    }

    const auto dungeon_config = dungeon_config_repository_.FindByDungeonId(request.dungeon_id);
    if (!dungeon_config.has_value()) {
        return {false, common::error::ErrorCode::kDungeonNotFound, "dungeon config not found", false, {}};
    }

    if (request.star <= 0 || request.star > dungeon_config->max_star) {
        return {false, common::error::ErrorCode::kInvalidStar, "star is out of range", false, {}};
    }

    if (!AcquirePlayerLock(request.player_id)) {
        return {false, common::error::ErrorCode::kPlayerBusy, "player is busy", false, {}};
    }

    auto battle_context = battle_context_repository_.FindByBattleId(request.battle_id);
    if (!battle_context.has_value()) {
        battle_context = dungeon_repository_.FindBattleById(request.battle_id);
    }
    if (!battle_context.has_value()) {
        ReleasePlayerLock(request.player_id);
        return {false, common::error::ErrorCode::kBattleNotFound, "battle not found", false, {}};
    }

    if (battle_context->player_id != request.player_id || battle_context->dungeon_id != request.dungeon_id) {
        ReleasePlayerLock(request.player_id);
        return {false, common::error::ErrorCode::kBattleMismatch, "battle context mismatch", false, {}};
    }

    if (battle_context->settled) {
        ReleasePlayerLock(request.player_id);
        return {false, common::error::ErrorCode::kBattleAlreadySettled, "battle already settled", false, {}};
    }

    const auto settle_result = dungeon_repository_.SettleDungeon(*battle_context, *dungeon_config, request.star);
    if (!settle_result.success) {
        ReleasePlayerLock(request.player_id);
        const auto code = settle_result.error_message == "battle already settled"
                              ? common::error::ErrorCode::kBattleAlreadySettled
                              : common::error::ErrorCode::kStorageError;
        return {false, code, settle_result.error_message, false, {}};
    }

    battle_context_repository_.Delete(request.battle_id);
    player_cache_repository_.Invalidate(request.player_id);
    ReleasePlayerLock(request.player_id);
    return {true, common::error::ErrorCode::kOk, "", settle_result.first_clear, settle_result.rewards};
}

std::string DungeonService::GenerateBattleId(std::int64_t player_id, int dungeon_id) {
    std::ostringstream output;
    output << "battle-" << player_id << '-' << dungeon_id << '-' << sequence_++;
    return output.str();
}

std::string DungeonService::PlayerLockKey(std::int64_t player_id) const {
    return "player:lock:" + std::to_string(player_id);
}

bool DungeonService::AcquirePlayerLock(std::int64_t player_id) {
    bool inserted = false;
    return redis_client_.SetNxWithExpire(PlayerLockKey(player_id), "1", 10, &inserted) && inserted;
}

void DungeonService::ReleasePlayerLock(std::int64_t player_id) {
    redis_client_.Del(PlayerLockKey(player_id));
}

}  // namespace dungeon_server::dungeon
