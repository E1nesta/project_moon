#pragma once

#include "modules/dungeon/ports/player_snapshot_port.h"
#include "internal/player_internal.grpc.pb.h"

#include <grpcpp/channel.h>

#include <memory>

namespace dungeon_server::dungeon {

class GrpcPlayerSnapshotPort final : public PlayerSnapshotPort {
public:
    explicit GrpcPlayerSnapshotPort(std::shared_ptr<::grpc::Channel> channel);

    [[nodiscard]] std::optional<PlayerSnapshot> GetBattleEntrySnapshot(std::int64_t player_id) const override;
    bool InvalidatePlayerSnapshot(std::int64_t player_id) override;
    [[nodiscard]] PrepareBattleEntryPortResponse PrepareBattleEntry(std::int64_t player_id,
                                                                    std::int64_t session_id,
                                                                    int energy_cost,
                                                                    const std::string& idempotency_key) override;
    [[nodiscard]] CancelBattleEntryPortResponse CancelBattleEntry(std::int64_t player_id,
                                                                  std::int64_t session_id,
                                                                  int energy_refund,
                                                                  const std::string& idempotency_key) override;
    [[nodiscard]] ApplyRewardGrantPortResponse ApplyRewardGrant(std::int64_t player_id,
                                                                std::int64_t grant_id,
                                                                std::int64_t session_id,
                                                                const std::vector<common::model::Reward>& rewards,
                                                                const std::string& idempotency_key) override;

private:
    std::unique_ptr<game_backend::internal::player::PlayerInternal::Stub> stub_;
};

}  // namespace dungeon_server::dungeon
