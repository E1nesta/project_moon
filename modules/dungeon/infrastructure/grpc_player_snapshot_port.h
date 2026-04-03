#pragma once

#include "modules/dungeon/ports/player_snapshot_port.h"
#include "internal/player_internal.grpc.pb.h"

#include <grpcpp/channel.h>

#include <memory>

namespace dungeon_server::dungeon {

class GrpcPlayerSnapshotPort final : public PlayerSnapshotPort {
public:
    explicit GrpcPlayerSnapshotPort(std::shared_ptr<::grpc::Channel> channel);

    [[nodiscard]] std::optional<PlayerSnapshot> LoadPlayerSnapshot(std::int64_t player_id) const override;
    bool InvalidatePlayerSnapshot(std::int64_t player_id) override;

private:
    std::unique_ptr<game_backend::internal::player::PlayerInternal::Stub> stub_;
};

}  // namespace dungeon_server::dungeon
