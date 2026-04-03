#include "modules/player/interfaces/grpc/player_internal_service_impl.h"

namespace game_server::player {

PlayerInternalServiceImpl::PlayerInternalServiceImpl(PlayerService& player_service) : player_service_(player_service) {}

::grpc::Status PlayerInternalServiceImpl::GetPlayerSnapshot(
    ::grpc::ServerContext* /*context*/,
    const game_backend::internal::player::GetPlayerSnapshotRequest* request,
    game_backend::internal::player::GetPlayerSnapshotResponse* response) {
    if (request == nullptr || response == nullptr) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "request and response are required");
    }
    if (request->player_id() <= 0) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "player_id must be positive");
    }

    const auto result = player_service_.GetPlayerSnapshot(request->player_id());
    if (!result.success) {
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, result.error_message);
    }

    response->set_found(result.found);
    if (result.found) {
        response->set_player_id(result.player_id);
        response->set_level(result.level);
        response->set_stamina(result.stamina);
    }
    return ::grpc::Status::OK;
}

::grpc::Status PlayerInternalServiceImpl::InvalidatePlayerCache(
    ::grpc::ServerContext* /*context*/,
    const game_backend::internal::player::InvalidatePlayerCacheRequest* request,
    game_backend::internal::player::InvalidatePlayerCacheResponse* response) {
    if (request == nullptr || response == nullptr) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "request and response are required");
    }
    if (request->player_id() <= 0) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "player_id must be positive");
    }

    const auto result = player_service_.InvalidatePlayerCache(request->player_id());
    if (!result.success) {
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, result.error_message);
    }

    response->set_success(true);
    return ::grpc::Status::OK;
}

}  // namespace game_server::player
