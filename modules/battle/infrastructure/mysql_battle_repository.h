#pragma once

#include "modules/battle/domain/battle_context.h"
#include "modules/battle/domain/reward.h"
#include "modules/battle/domain/stage_config.h"
#include "modules/battle/ports/battle_repository.h"
#include "runtime/storage/mysql/mysql_client_pool.h"

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace battle_server::battle {

class MySqlBattleRepository final : public BattleRepository {
public:
    explicit MySqlBattleRepository(common::mysql::MySqlClientPool& mysql_pool);
    explicit MySqlBattleRepository(common::mysql::MySqlClient& mysql_client);
    ~MySqlBattleRepository() override = default;

    [[nodiscard]] std::optional<common::model::BattleContext> FindBattleById(std::int64_t session_id) const override;
    [[nodiscard]] std::optional<common::model::BattleContext> FindUnsettledBattleByPlayerId(
        std::int64_t player_id) const override;
    [[nodiscard]] EnterBattleResult CreateBattleSession(std::int64_t session_id,
                                                        std::int64_t player_id,
                                                        int stage_id,
                                                        const std::string& mode,
                                                        int cost_energy,
                                                        int remain_energy_after,
                                                        const std::vector<BattleRoleSummary>& role_summaries,
                                                        std::int64_t seed,
                                                        const std::string& idempotency_key,
                                                        const std::string& trace_id) override;
    bool CancelBattleSession(std::int64_t session_id, std::string* error_message = nullptr) override;
    [[nodiscard]] SettleBattleResult RecordBattleSettlement(std::int64_t session_id,
                                                            std::int64_t player_id,
                                                            int stage_id,
                                                            int result_code,
                                                            int star,
                                                            std::int64_t client_score,
                                                            std::int64_t reward_grant_id,
                                                            const std::vector<common::model::Reward>& rewards,
                                                            const std::string& idempotency_key) override;
    [[nodiscard]] SettleBattleResult MarkRewardGrantGranted(std::int64_t reward_grant_id) override;
    [[nodiscard]] RewardGrantStatusResult GetRewardGrantStatus(std::int64_t reward_grant_id) const override;

private:
    [[nodiscard]] static std::string CurrentMonthSuffix();
    [[nodiscard]] static std::string SessionTable();
    [[nodiscard]] static std::string TeamSnapshotTable();
    [[nodiscard]] static std::string ResultTable();
    [[nodiscard]] static std::string RewardGrantTable();

    common::mysql::MySqlClientPool* mysql_pool_ = nullptr;
    common::mysql::MySqlClient* mysql_client_ = nullptr;
};

}  // namespace battle_server::battle
