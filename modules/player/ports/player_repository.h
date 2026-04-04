#pragma once

#include "modules/player/domain/player_state.h"
#include "modules/dungeon/domain/reward.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace game_server::player {

enum class PlayerMutationError {
    kNone,
    kPlayerNotFound,
    kStaminaNotEnough,
    kBattleMismatch,
    kAlreadyApplied,
    kStorageFailure,
};

struct BattleEntrySnapshotResult {
    bool success = false;
    bool found = false;
    int level = 0;
    int energy = 0;
    std::vector<common::model::PlayerRoleSummary> role_summaries;
    PlayerMutationError error = PlayerMutationError::kNone;
    std::string error_message;
};

struct PrepareBattleEntryResult {
    bool success = false;
    int remain_energy = 0;
    PlayerMutationError error = PlayerMutationError::kNone;
    std::string error_message;
};

struct CancelBattleEntryResult {
    bool success = false;
    PlayerMutationError error = PlayerMutationError::kNone;
    std::string error_message;
};

struct ApplyRewardGrantResult {
    bool success = false;
    std::vector<common::model::CurrencyBalance> applied_currencies;
    PlayerMutationError error = PlayerMutationError::kNone;
    std::string error_message;
};

// Storage boundary for player state reads from the system of record.
class PlayerRepository {
public:
    virtual ~PlayerRepository() = default;

    [[nodiscard]] virtual std::optional<common::model::PlayerState> LoadPlayerState(std::int64_t player_id) const = 0;
    [[nodiscard]] virtual BattleEntrySnapshotResult GetBattleEntrySnapshot(std::int64_t player_id) const = 0;
    [[nodiscard]] virtual PrepareBattleEntryResult PrepareBattleEntry(std::int64_t player_id,
                                                                      std::int64_t session_id,
                                                                      int energy_cost,
                                                                      const std::string& idempotency_key) = 0;
    [[nodiscard]] virtual CancelBattleEntryResult CancelBattleEntry(std::int64_t player_id,
                                                                    std::int64_t session_id,
                                                                    int energy_refund,
                                                                    const std::string& idempotency_key) = 0;
    [[nodiscard]] virtual ApplyRewardGrantResult ApplyRewardGrant(
        std::int64_t player_id,
        std::int64_t grant_id,
        std::int64_t session_id,
        const std::vector<common::model::Reward>& rewards,
        const std::string& idempotency_key) = 0;
};

}  // namespace game_server::player
