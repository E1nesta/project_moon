#include "modules/dungeon/infrastructure/grpc_player_snapshot_port.h"

#include "runtime/foundation/log/logger.h"

#include <chrono>

namespace dungeon_server::dungeon {

namespace {

constexpr auto kRpcTimeout = std::chrono::milliseconds(2000);

}  // namespace

GrpcPlayerSnapshotPort::GrpcPlayerSnapshotPort(std::shared_ptr<::grpc::Channel> channel)
    : stub_(game_backend::internal::player::PlayerInternal::NewStub(std::move(channel))) {}

std::optional<PlayerSnapshot> GrpcPlayerSnapshotPort::LoadPlayerSnapshot(std::int64_t player_id) const {
    if (player_id <= 0 || stub_ == nullptr) {
        return std::nullopt;
    }

    game_backend::internal::player::GetPlayerSnapshotRequest request;
    request.set_player_id(player_id);
    game_backend::internal::player::GetPlayerSnapshotResponse response;
    ::grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + kRpcTimeout);

    const auto status = stub_->GetPlayerSnapshot(&context, request, &response);
    if (!status.ok() || !response.found()) {
        return std::nullopt;
    }

    PlayerSnapshot snapshot;
    snapshot.player_id = response.player_id();
    snapshot.level = response.level();
    snapshot.stamina = response.stamina();
    return snapshot;
}

bool GrpcPlayerSnapshotPort::InvalidatePlayerSnapshot(std::int64_t player_id) {
    if (player_id <= 0 || stub_ == nullptr) {
        return false;
    }

    game_backend::internal::player::InvalidatePlayerCacheRequest request;
    request.set_player_id(player_id);
    game_backend::internal::player::InvalidatePlayerCacheResponse response;
    ::grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + kRpcTimeout);

    const auto status = stub_->InvalidatePlayerCache(&context, request, &response);
    if (!status.ok() || !response.success()) {
        common::log::Logger::Instance().Log(
            common::log::LogLevel::kWarn,
            "grpc invalidate player cache failed for player_id=" + std::to_string(player_id));
        return false;
    }
    return true;
}

}  // namespace dungeon_server::dungeon
