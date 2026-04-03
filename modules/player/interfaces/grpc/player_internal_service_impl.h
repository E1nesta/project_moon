#pragma once

#include "modules/player/application/player_service.h"
#include "internal/player_internal.grpc.pb.h"

namespace game_server::player {

class PlayerInternalServiceImpl final : public game_backend::internal::player::PlayerInternal::Service {
public:
    explicit PlayerInternalServiceImpl(PlayerService& player_service);

    ::grpc::Status GetPlayerSnapshot(::grpc::ServerContext* context,
                                     const game_backend::internal::player::GetPlayerSnapshotRequest* request,
                                     game_backend::internal::player::GetPlayerSnapshotResponse* response) override;

    ::grpc::Status InvalidatePlayerCache(::grpc::ServerContext* context,
                                         const game_backend::internal::player::InvalidatePlayerCacheRequest* request,
                                         game_backend::internal::player::InvalidatePlayerCacheResponse* response) override;

private:
    PlayerService& player_service_;
};

}  // namespace game_server::player
