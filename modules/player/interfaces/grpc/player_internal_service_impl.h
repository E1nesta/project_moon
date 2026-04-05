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
    ::grpc::Status GetBattleEntrySnapshot(::grpc::ServerContext* context,
                                          const game_backend::internal::player::GetBattleEntrySnapshotRequest* request,
                                          game_backend::internal::player::GetBattleEntrySnapshotResponse* response) override;
    ::grpc::Status PrepareBattleEntry(::grpc::ServerContext* context,
                                      const game_backend::internal::player::PrepareBattleEntryRequest* request,
                                      game_backend::internal::player::PrepareBattleEntryResponse* response) override;
    ::grpc::Status CancelBattleEntry(::grpc::ServerContext* context,
                                     const game_backend::internal::player::CancelBattleEntryRequest* request,
                                     game_backend::internal::player::CancelBattleEntryResponse* response) override;
    ::grpc::Status ApplyRewardGrant(::grpc::ServerContext* context,
                                    const game_backend::internal::player::ApplyRewardGrantRequest* request,
                                    game_backend::internal::player::ApplyRewardGrantResponse* response) override;

private:
    PlayerService& player_service_;
};

}  // namespace game_server::player
