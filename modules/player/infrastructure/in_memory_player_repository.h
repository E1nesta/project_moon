#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "modules/player/ports/player_repository.h"

#include <string>
#include <unordered_map>

namespace game_server::player {

class InMemoryPlayerRepository final : public PlayerRepository {
public:
    static InMemoryPlayerRepository FromConfig(const common::config::SimpleConfig& config);

    [[nodiscard]] std::optional<common::model::PlayerState> LoadPlayerState(std::int64_t player_id) const override;
    [[nodiscard]] BattleEntrySnapshotResult GetBattleEntrySnapshot(std::int64_t player_id) const override;
    [[nodiscard]] PrepareBattleEntryResult PrepareBattleEntry(std::int64_t player_id,
                                                              std::int64_t session_id,
                                                              int energy_cost,
                                                              const std::string& idempotency_key) override;
    [[nodiscard]] CancelBattleEntryResult CancelBattleEntry(std::int64_t player_id,
                                                            std::int64_t session_id,
                                                            int energy_refund,
                                                            const std::string& idempotency_key) override;
    [[nodiscard]] ApplyRewardGrantResult ApplyRewardGrant(std::int64_t player_id,
                                                          std::int64_t grant_id,
                                                          std::int64_t session_id,
                                                          const std::vector<common::model::Reward>& rewards,
                                                          const std::string& idempotency_key) override;

private:
    struct EnterOperationRecord {
        std::int64_t player_id = 0;
        int energy_cost = 0;
        PrepareBattleEntryResult result;
    };

    struct RewardGrantOperationRecord {
        std::int64_t player_id = 0;
        std::int64_t grant_id = 0;
        ApplyRewardGrantResult result;
    };

    explicit InMemoryPlayerRepository(common::model::PlayerState player_state);

    std::unordered_map<std::string, EnterOperationRecord> enter_operations_;
    std::unordered_map<std::string, RewardGrantOperationRecord> reward_operations_;
    std::unordered_map<std::int64_t, common::model::PlayerState> players_;
};

}  // namespace game_server::player
