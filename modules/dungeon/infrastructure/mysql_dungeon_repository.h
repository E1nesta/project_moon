#pragma once

#include "modules/dungeon/domain/battle_context.h"
#include "modules/dungeon/domain/reward.h"
#include "modules/dungeon/domain/dungeon_config.h"
#include "modules/dungeon/ports/dungeon_repository.h"
#include "runtime/storage/mysql/mysql_client_pool.h"

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace dungeon_server::dungeon {

class MySqlDungeonRepository final : public DungeonRepository {
public:
    explicit MySqlDungeonRepository(common::mysql::MySqlClientPool& mysql_pool);
    explicit MySqlDungeonRepository(common::mysql::MySqlClient& mysql_client);
    ~MySqlDungeonRepository() override = default;

    [[nodiscard]] std::optional<common::model::BattleContext> FindBattleById(std::int64_t session_id) const override;
    [[nodiscard]] std::optional<common::model::BattleContext> FindUnsettledBattleByPlayerId(
        std::int64_t player_id) const override;
    [[nodiscard]] EnterBattleResult CreateBattleSession(std::int64_t session_id,
                                                        std::int64_t player_id,
                                                        int stage_id,
                                                        const std::string& mode,
                                                        int cost_energy,
                                                        int remain_energy_after,
                                                        const std::vector<common::model::PlayerRoleSummary>& role_summaries,
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

}  // namespace dungeon_server::dungeon
