#pragma once

#include "modules/battle/domain/reward.h"
#include "runtime/foundation/error/error_code.h"
#include "modules/battle/domain/player_snapshot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace battle_server::battle {

struct GetBattleEntrySnapshotPortResponse {
    bool success = false;
    bool found = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    PlayerSnapshot snapshot;
};

struct PrepareBattleEntryPortResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    int remain_energy = 0;
};

struct CancelBattleEntryPortResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
};

struct ApplyRewardGrantPortResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    std::vector<common::model::Reward> rewards;
};

class PlayerSnapshotPort {
public:
    virtual ~PlayerSnapshotPort() = default;

    [[nodiscard]] virtual GetBattleEntrySnapshotPortResponse GetBattleEntrySnapshot(std::int64_t player_id) const = 0;
    virtual bool InvalidatePlayerSnapshot(std::int64_t player_id) = 0;
    [[nodiscard]] virtual PrepareBattleEntryPortResponse PrepareBattleEntry(std::int64_t player_id,
                                                                            std::int64_t session_id,
                                                                            int energy_cost,
                                                                            const std::string& idempotency_key) = 0;
    [[nodiscard]] virtual CancelBattleEntryPortResponse CancelBattleEntry(std::int64_t player_id,
                                                                          std::int64_t session_id,
                                                                          int energy_refund,
                                                                          const std::string& idempotency_key) = 0;
    [[nodiscard]] virtual ApplyRewardGrantPortResponse ApplyRewardGrant(std::int64_t player_id,
                                                                        std::int64_t grant_id,
                                                                        std::int64_t session_id,
                                                                        const std::vector<common::model::Reward>& rewards,
                                                                        const std::string& idempotency_key) = 0;
};

}  // namespace battle_server::battle
