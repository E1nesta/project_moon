#include "modules/dungeon/infrastructure/grpc_player_snapshot_port.h"

#include <chrono>

namespace dungeon_server::dungeon {

namespace {

constexpr auto kRpcTimeout = std::chrono::milliseconds(2000);

}  // namespace

GrpcPlayerSnapshotPort::GrpcPlayerSnapshotPort(std::shared_ptr<::grpc::Channel> channel)
    : stub_(game_backend::internal::player::PlayerInternal::NewStub(std::move(channel))) {}

std::optional<PlayerSnapshot> GrpcPlayerSnapshotPort::GetBattleEntrySnapshot(std::int64_t player_id) const {
    if (player_id <= 0 || stub_ == nullptr) {
        return std::nullopt;
    }

    game_backend::internal::player::GetBattleEntrySnapshotRequest request;
    request.set_player_id(player_id);
    game_backend::internal::player::GetBattleEntrySnapshotResponse response;
    ::grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + kRpcTimeout);
    const auto status = stub_->GetBattleEntrySnapshot(&context, request, &response);
    if (!status.ok() || !response.found()) {
        return std::nullopt;
    }

    PlayerSnapshot snapshot;
    snapshot.player_id = response.player_id();
    snapshot.level = response.level();
    snapshot.stamina = response.energy();
    for (const auto& role_summary : response.role_summaries()) {
        snapshot.role_summaries.push_back({role_summary.role_id(), role_summary.level(), role_summary.star()});
    }
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
    return status.ok() && response.success();
}

PrepareBattleEntryPortResponse GrpcPlayerSnapshotPort::PrepareBattleEntry(std::int64_t player_id,
                                                                          std::int64_t session_id,
                                                                          int energy_cost,
                                                                          const std::string& idempotency_key) {
    game_backend::internal::player::PrepareBattleEntryRequest request;
    request.set_player_id(player_id);
    request.set_session_id(session_id);
    request.set_energy_cost(energy_cost);
    request.set_idempotency_key(idempotency_key);
    game_backend::internal::player::PrepareBattleEntryResponse response;
    ::grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + kRpcTimeout);

    const auto status = stub_->PrepareBattleEntry(&context, request, &response);
    if (status.ok()) {
        return {true, common::error::ErrorCode::kOk, "", response.remain_energy()};
    }
    if (status.error_code() == ::grpc::StatusCode::NOT_FOUND) {
        return {false, common::error::ErrorCode::kPlayerNotFound, status.error_message(), 0};
    }
    if (status.error_code() == ::grpc::StatusCode::FAILED_PRECONDITION) {
        return {false, common::error::ErrorCode::kStaminaNotEnough, status.error_message(), 0};
    }
    if (status.error_code() == ::grpc::StatusCode::ABORTED) {
        return {false, common::error::ErrorCode::kBattleMismatch, status.error_message(), 0};
    }
    return {false, common::error::ErrorCode::kStorageError, status.error_message(), 0};
}

CancelBattleEntryPortResponse GrpcPlayerSnapshotPort::CancelBattleEntry(std::int64_t player_id,
                                                                        std::int64_t session_id,
                                                                        int energy_refund,
                                                                        const std::string& idempotency_key) {
    game_backend::internal::player::CancelBattleEntryRequest request;
    request.set_player_id(player_id);
    request.set_session_id(session_id);
    request.set_energy_refund(energy_refund);
    request.set_idempotency_key(idempotency_key);
    game_backend::internal::player::CancelBattleEntryResponse response;
    ::grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + kRpcTimeout);
    const auto status = stub_->CancelBattleEntry(&context, request, &response);
    if (!status.ok() || !response.success()) {
        return {false, common::error::ErrorCode::kStorageError, status.error_message()};
    }
    return {true, common::error::ErrorCode::kOk, ""};
}

ApplyRewardGrantPortResponse GrpcPlayerSnapshotPort::ApplyRewardGrant(std::int64_t player_id,
                                                                      std::int64_t grant_id,
                                                                      std::int64_t session_id,
                                                                      const std::vector<common::model::Reward>& rewards,
                                                                      const std::string& idempotency_key) {
    game_backend::internal::player::ApplyRewardGrantRequest request;
    request.set_player_id(player_id);
    request.set_grant_id(grant_id);
    request.set_session_id(session_id);
    request.set_idempotency_key(idempotency_key);
    for (const auto& reward : rewards) {
        auto* item = request.add_rewards();
        item->set_reward_type(reward.reward_type);
        item->set_amount(reward.amount);
    }

    game_backend::internal::player::ApplyRewardGrantResponse response;
    ::grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + kRpcTimeout);
    const auto status = stub_->ApplyRewardGrant(&context, request, &response);
    if (!status.ok()) {
        const auto error_code = status.error_code() == ::grpc::StatusCode::NOT_FOUND
                                    ? common::error::ErrorCode::kPlayerNotFound
                                    : status.error_code() == ::grpc::StatusCode::ABORTED
                                          ? common::error::ErrorCode::kBattleMismatch
                                          : common::error::ErrorCode::kStorageError;
        return {false, error_code, status.error_message(), {}};
    }

    ApplyRewardGrantPortResponse result;
    result.success = true;
    for (const auto& reward : response.rewards()) {
        result.rewards.push_back({reward.reward_type(), reward.amount()});
    }
    return result;
}

}  // namespace dungeon_server::dungeon
