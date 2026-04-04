#include "modules/player/interfaces/grpc/player_internal_service_impl.h"

namespace game_server::player {

namespace {

std::int64_t ParseLegacyNumericId(const std::string& raw_value) {
    if (raw_value.empty()) {
        return 0;
    }
    try {
        return std::stoll(raw_value);
    } catch (...) {
        return 0;
    }
}

}  // namespace

PlayerInternalServiceImpl::PlayerInternalServiceImpl(PlayerService& player_service) : player_service_(player_service) {}

::grpc::Status PlayerInternalServiceImpl::GetPlayerSnapshot(
    ::grpc::ServerContext* /*context*/,
    const game_backend::internal::player::GetPlayerSnapshotRequest* request,
    game_backend::internal::player::GetPlayerSnapshotResponse* response) {
    if (request == nullptr || response == nullptr) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "request and response are required");
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
    const auto result = player_service_.InvalidatePlayerCache(request->player_id());
    if (!result.success) {
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, result.error_message);
    }
    response->set_success(true);
    return ::grpc::Status::OK;
}

::grpc::Status PlayerInternalServiceImpl::GetBattleEntrySnapshot(
    ::grpc::ServerContext* /*context*/,
    const game_backend::internal::player::GetBattleEntrySnapshotRequest* request,
    game_backend::internal::player::GetBattleEntrySnapshotResponse* response) {
    if (request == nullptr || response == nullptr) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "request and response are required");
    }
    const auto result = player_service_.GetBattleEntrySnapshot(request->player_id());
    if (!result.success) {
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, result.error_message);
    }
    response->set_found(result.found);
    if (result.found) {
        response->set_player_id(result.player_id);
        response->set_level(result.level);
        response->set_energy(result.energy);
        for (const auto& role_summary : result.role_summaries) {
            auto* item = response->add_role_summaries();
            item->set_role_id(role_summary.role_id);
            item->set_level(role_summary.level);
            item->set_star(role_summary.star);
        }
    }
    return ::grpc::Status::OK;
}

::grpc::Status PlayerInternalServiceImpl::PrepareBattleEntry(
    ::grpc::ServerContext* /*context*/,
    const game_backend::internal::player::PrepareBattleEntryRequest* request,
    game_backend::internal::player::PrepareBattleEntryResponse* response) {
    if (request == nullptr || response == nullptr || request->player_id() <= 0 || request->session_id() <= 0 ||
        request->energy_cost() <= 0 || request->idempotency_key().empty()) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "invalid prepare battle entry request");
    }
    const auto result = player_service_.PrepareBattleEntry(
        request->player_id(), request->session_id(), request->energy_cost(), request->idempotency_key());
    if (!result.success) {
        if (result.error_code == common::error::ErrorCode::kPlayerNotFound) {
            return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, result.error_message);
        }
        if (result.error_code == common::error::ErrorCode::kStaminaNotEnough) {
            return ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION, result.error_message);
        }
        if (result.error_code == common::error::ErrorCode::kBattleMismatch) {
            return ::grpc::Status(::grpc::StatusCode::ABORTED, result.error_message);
        }
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, result.error_message);
    }
    response->set_remain_energy(result.remain_energy);
    return ::grpc::Status::OK;
}

::grpc::Status PlayerInternalServiceImpl::CancelBattleEntry(
    ::grpc::ServerContext* /*context*/,
    const game_backend::internal::player::CancelBattleEntryRequest* request,
    game_backend::internal::player::CancelBattleEntryResponse* response) {
    if (request == nullptr || response == nullptr || request->player_id() <= 0 || request->session_id() <= 0 ||
        request->energy_refund() < 0 || request->idempotency_key().empty()) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "invalid cancel battle entry request");
    }
    const auto result = player_service_.CancelBattleEntry(
        request->player_id(), request->session_id(), request->energy_refund(), request->idempotency_key());
    if (!result.success) {
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, result.error_message);
    }
    response->set_success(true);
    return ::grpc::Status::OK;
}

::grpc::Status PlayerInternalServiceImpl::ApplyRewardGrant(
    ::grpc::ServerContext* /*context*/,
    const game_backend::internal::player::ApplyRewardGrantRequest* request,
    game_backend::internal::player::ApplyRewardGrantResponse* response) {
    if (request == nullptr || response == nullptr || request->player_id() <= 0 || request->grant_id() <= 0 ||
        request->session_id() <= 0 || request->idempotency_key().empty()) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "invalid apply reward grant request");
    }

    std::vector<common::model::Reward> rewards;
    rewards.reserve(static_cast<std::size_t>(request->rewards_size()));
    for (const auto& reward : request->rewards()) {
        rewards.push_back({reward.reward_type(), reward.amount()});
    }

    const auto result = player_service_.ApplyRewardGrant(
        request->player_id(), request->grant_id(), request->session_id(), rewards, request->idempotency_key());
    if (!result.success) {
        if (result.error_code == common::error::ErrorCode::kPlayerNotFound) {
            return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, result.error_message);
        }
        if (result.error_code == common::error::ErrorCode::kBattleMismatch) {
            return ::grpc::Status(::grpc::StatusCode::ABORTED, result.error_message);
        }
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, result.error_message);
    }

    for (const auto& currency : result.applied_currencies) {
        auto* reward = response->add_rewards();
        reward->set_reward_type(currency.currency_type);
        reward->set_amount(currency.amount);
    }
    return ::grpc::Status::OK;
}

::grpc::Status PlayerInternalServiceImpl::SpendStaminaForDungeonEnter(
    ::grpc::ServerContext* context,
    const game_backend::internal::player::SpendStaminaForDungeonEnterRequest* request,
    game_backend::internal::player::SpendStaminaForDungeonEnterResponse* response) {
    game_backend::internal::player::PrepareBattleEntryRequest translated;
    translated.set_player_id(request->player_id());
    translated.set_session_id(ParseLegacyNumericId(request->battle_id()));
    translated.set_energy_cost(request->stamina_cost());
    translated.set_idempotency_key("legacy-enter:" + request->battle_id());
    game_backend::internal::player::PrepareBattleEntryResponse translated_response;
    const auto status = PrepareBattleEntry(context, &translated, &translated_response);
    if (status.ok()) {
        response->set_remain_stamina(translated_response.remain_energy());
    }
    return status;
}

::grpc::Status PlayerInternalServiceImpl::ApplyDungeonSettlement(
    ::grpc::ServerContext* context,
    const game_backend::internal::player::ApplyDungeonSettlementRequest* request,
    game_backend::internal::player::ApplyDungeonSettlementResponse* response) {
    game_backend::internal::player::ApplyRewardGrantRequest translated;
    translated.set_player_id(request->player_id());
    translated.set_grant_id(ParseLegacyNumericId(request->battle_id()));
    translated.set_session_id(ParseLegacyNumericId(request->battle_id()));
    translated.set_idempotency_key("legacy-settle:" + request->battle_id());
    auto* gold = translated.add_rewards();
    gold->set_reward_type("gold");
    gold->set_amount(request->normal_gold_reward());
    if (request->first_clear_diamond_reward() > 0) {
        auto* diamond = translated.add_rewards();
        diamond->set_reward_type("diamond");
        diamond->set_amount(request->first_clear_diamond_reward());
    }
    game_backend::internal::player::ApplyRewardGrantResponse translated_response;
    const auto status = ApplyRewardGrant(context, &translated, &translated_response);
    if (!status.ok()) {
        return status;
    }

    response->set_first_clear(request->first_clear_diamond_reward() > 0);
    for (const auto& reward : translated_response.rewards()) {
        auto* item = response->add_rewards();
        item->set_reward_type(reward.reward_type());
        item->set_amount(reward.amount());
    }
    return ::grpc::Status::OK;
}

}  // namespace game_server::player
