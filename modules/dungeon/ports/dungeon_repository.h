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
    kGrantNotFound,
};

struct EnterBattleResult {
    bool success = false;
    DungeonRepositoryError error = DungeonRepositoryError::kNone;
    std::string error_message;
    common::model::BattleContext battle_context;
};

struct SettleBattleResult {
    bool success = false;
    DungeonRepositoryError error = DungeonRepositoryError::kNone;
    std::string error_message;
};

struct RewardGrantStatusResult {
    bool success = false;
    int grant_status = 0;
    std::vector<common::model::Reward> rewards;
    DungeonRepositoryError error = DungeonRepositoryError::kNone;
    std::string error_message;
};

class DungeonRepository {
public:
    virtual ~DungeonRepository() = default;

    [[nodiscard]] virtual std::optional<common::model::BattleContext> FindBattleById(std::int64_t session_id) const = 0;
    [[nodiscard]] virtual std::optional<common::model::BattleContext> FindUnsettledBattleByPlayerId(
        std::int64_t player_id) const = 0;
    [[nodiscard]] virtual EnterBattleResult CreateBattleSession(std::int64_t session_id,
                                                                std::int64_t player_id,
                                                                int stage_id,
                                                                const std::string& mode,
                                                                int cost_energy,
                                                                int remain_energy_after,
                                                                const std::vector<common::model::PlayerRoleSummary>& role_summaries,
                                                                std::int64_t seed,
                                                                const std::string& idempotency_key,
                                                                const std::string& trace_id) = 0;
    virtual bool CancelBattleSession(std::int64_t session_id, std::string* error_message = nullptr) = 0;
    [[nodiscard]] virtual SettleBattleResult RecordBattleSettlement(std::int64_t session_id,
                                                                    std::int64_t player_id,
                                                                    int stage_id,
                                                                    int result_code,
                                                                    int star,
                                                                    std::int64_t client_score,
                                                                    std::int64_t reward_grant_id,
                                                                    const std::vector<common::model::Reward>& rewards,
                                                                    const std::string& idempotency_key) = 0;
    [[nodiscard]] virtual RewardGrantStatusResult GetRewardGrantStatus(std::int64_t reward_grant_id) const = 0;
};

}  // namespace dungeon_server::dungeon
