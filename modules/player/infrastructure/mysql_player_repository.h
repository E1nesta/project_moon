#pragma once

#include "runtime/storage/mysql/mysql_client_pool.h"
#include "modules/player/ports/player_repository.h"

#include <atomic>

namespace game_server::player {

// MySQL-backed implementation of the player state storage boundary.
class MySqlPlayerRepository final : public PlayerRepository {
public:
    explicit MySqlPlayerRepository(common::mysql::MySqlClientPool& mysql_pool);

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
    [[nodiscard]] static std::string ShardSuffix(std::int64_t player_id);
    [[nodiscard]] static std::string ProfileTable(std::int64_t player_id);
    [[nodiscard]] static std::string CurrencyTable(std::int64_t player_id);
    [[nodiscard]] static std::string CurrencyTxnTable(std::int64_t player_id);
    [[nodiscard]] static std::string RoleTable(std::int64_t player_id);
    [[nodiscard]] static std::string ItemTxnTable(std::int64_t player_id);
    [[nodiscard]] static std::string PlayerOutboxTable(std::int64_t player_id);
    [[nodiscard]] static std::string CurrencyIdempotencyKey(const std::string& idempotency_key,
                                                            const std::string& currency_type);

    common::mysql::MySqlClientPool& mysql_pool_;
};

}  // namespace game_server::player
