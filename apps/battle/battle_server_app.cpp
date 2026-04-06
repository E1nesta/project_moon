#include "apps/battle/battle_server_app.h"

#include "runtime/foundation/log/logger.h"
#include "runtime/grpc/channel_factory.h"
#include "runtime/protocol/adapter_utils.h"
#include "runtime/protocol/proto_mapper.h"

#include "game_backend.pb.h"

#include <functional>

namespace services::battle {

namespace {

bool RequestPlayerMatchesContext(const framework::protocol::HandlerContext& context, std::int64_t request_player_id) {
    return request_player_id == 0 || request_player_id == context.request.player_id;
}

bool UseInternalGrpcMtls(const common::config::SimpleConfig& config) {
    const auto environment = config.GetString("runtime.environment", "local");
    return environment == "staging" || environment == "prod";
}

framework::grpc::TlsChannelOptions BuildInternalGrpcTlsOptions() {
    framework::grpc::TlsChannelOptions options;
    options.root_cert_file = "/etc/game_backend/tls/ca.pem";
    options.cert_chain_file = "/etc/game_backend/tls/battle_server.crt";
    options.private_key_file = "/etc/game_backend/tls/battle_server.key";
    options.server_name = "player_internal_grpc_server";
    return options;
}

std::uint16_t BuildIdGeneratorNodeId(const common::config::SimpleConfig& config) {
    const auto instance_id = config.GetString("service.instance_id", config.GetString("service.name", "battle_server"));
    const auto hashed = std::hash<std::string>{}(instance_id);
    return static_cast<std::uint16_t>((hashed % 1023U) + 1U);
}

std::string BuildSettleTokenSecret(const common::config::SimpleConfig& config) {
    return config.GetString(
        "security.settle_token.secret",
        config.GetString("security.trusted_gateway.shared_secret", "battle-settle-token"));
}

void FillProtoReward(const common::model::Reward& reward, game_backend::proto::Reward* output) {
    if (output == nullptr) {
        return;
    }
    output->set_reward_type(reward.reward_type);
    output->set_amount(reward.amount);
}

common::net::Packet BuildEnterBattleResponsePacket(const framework::protocol::HandlerContext& context,
                                                   const battle_server::battle::EnterBattleResponse& result) {
    game_backend::proto::EnterBattleResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_session_id(result.session_id);
    response.set_remain_stamina(result.remain_stamina);
    response.set_seed(result.seed);
    response.set_settle_token(result.settle_token);
    return common::net::BuildPacket(common::net::MessageId::kEnterBattleResponse, context.request.request_id, response);
}

common::net::Packet BuildSettleBattleResponsePacket(const framework::protocol::HandlerContext& context,
                                                    const battle_server::battle::SettleBattleResponse& result) {
    game_backend::proto::SettleBattleResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_reward_grant_id(result.reward_grant_id);
    response.set_grant_status(result.grant_status);
    for (const auto& reward : result.reward_preview) {
        FillProtoReward(reward, response.add_reward_preview());
    }
    return common::net::BuildPacket(common::net::MessageId::kSettleBattleResponse, context.request.request_id, response);
}

common::net::Packet BuildGetRewardGrantStatusResponsePacket(
    const framework::protocol::HandlerContext& context,
    const battle_server::battle::RewardGrantStatusResponse& result) {
    game_backend::proto::GetRewardGrantStatusResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_reward_grant_id(result.reward_grant_id);
    response.set_grant_status(result.grant_status);
    for (const auto& reward : result.rewards) {
        FillProtoReward(reward, response.add_granted_rewards());
    }
    return common::net::BuildPacket(
        common::net::MessageId::kGetRewardGrantStatusResponse, context.request.request_id, response);
}

common::net::Packet BuildGetActiveBattleResponsePacket(
    const framework::protocol::HandlerContext& context,
    const battle_server::battle::ActiveBattleResponse& result) {
    game_backend::proto::GetActiveBattleResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_found(result.found);
    if (result.found) {
        response.set_session_id(result.session_id);
        response.set_stage_id(result.stage_id);
        response.set_mode(result.mode);
        response.set_remain_stamina(result.remain_stamina);
        response.set_seed(result.seed);
        response.set_settle_token(result.settle_token);
    }
    return common::net::BuildPacket(common::net::MessageId::kGetActiveBattleResponse, context.request.request_id, response);
}

}  // namespace

BattleServerApp::BattleServerApp() : framework::service::ServiceApp("battle_server", "configs/battle_server.conf") {}

bool BattleServerApp::BuildDependencies(std::string* error_message) {
    mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(Config(), "storage.battle.mysql."),
        static_cast<std::size_t>(Config().GetInt("storage.battle.mysql.pool_size", 4)));
    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config(), "storage.battle.redis."),
        static_cast<std::size_t>(Config().GetInt("storage.battle.redis.pool_size", 4)));
    if (!mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    std::shared_ptr<::grpc::Channel> player_internal_channel;
    if (UseInternalGrpcMtls(Config())) {
        player_internal_channel = framework::grpc::CreateTlsChannel(
            framework::grpc::BuildAddress(Config(), "grpc.client.player_internal."),
            BuildInternalGrpcTlsOptions(),
            error_message);
        if (player_internal_channel == nullptr) {
            return false;
        }
    } else {
        player_internal_channel = framework::grpc::CreateInsecureChannel(Config(), "grpc.client.player_internal.");
    }

    player_snapshot_port_ =
        std::make_unique<battle_server::battle::GrpcPlayerSnapshotPort>(std::move(player_internal_channel));
    stage_config_repository_ = std::make_unique<battle_server::battle::InMemoryStageConfigRepository>(
        battle_server::battle::InMemoryStageConfigRepository::FromConfig(Config()));
    battle_repository_ = std::make_unique<battle_server::battle::MySqlBattleRepository>(*mysql_pool_);
    battle_context_repository_ = std::make_unique<battle_server::battle::RedisBattleContextRepository>(
        *redis_pool_, Config().GetInt("storage.battle.context_ttl_seconds", 3600));
    player_lock_repository_ = std::make_unique<battle_server::battle::RedisPlayerLockRepository>(
        *redis_pool_, Config().GetInt("storage.player.lock_ttl_seconds", 10));
    battle_service_ = std::make_unique<battle_server::battle::BattleService>(
        *player_lock_repository_,
        *player_snapshot_port_,
        *stage_config_repository_,
        *battle_repository_,
        *battle_context_repository_,
        BuildIdGeneratorNodeId(Config()),
        BuildSettleTokenSecret(Config()));
    return true;
}

void BattleServerApp::RegisterRoutes() {
    Routes().Register(common::net::MessageId::kEnterBattleRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleEnterBattleRequest(context, packet);
                      });
    Routes().Register(common::net::MessageId::kSettleBattleRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleSettleBattleRequest(context, packet);
                      });
    Routes().Register(common::net::MessageId::kGetActiveBattleRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleGetActiveBattleRequest(context, packet);
                      });
    Routes().Register(common::net::MessageId::kGetRewardGrantStatusRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleGetRewardGrantStatusRequest(context, packet);
                      });
}

common::net::Packet BattleServerApp::HandleEnterBattleRequest(const framework::protocol::HandlerContext& context,
                                                               const common::net::Packet& packet) const {
    game_backend::proto::EnterBattleRequest request;
    common::net::Packet error_response;
    if (!framework::protocol::ParseProtoRequest(
            context, packet, "invalid enter battle request", &request, &error_response)) {
        return error_response;
    }
    if (!RequestPlayerMatchesContext(context, request.player_id())) {
        return framework::protocol::BuildErrorResponse(
            context.request,
            common::error::ErrorCode::kRequestContextInvalid,
            "request player_id does not match authenticated session");
    }

    const auto result = battle_service_->EnterBattle(
        {context.request.player_id,
         context.request.request_id,
         request.stage_id(),
         request.mode().empty() ? "pve" : request.mode()},
        context.request.trace_id);
    if (!result.success) {
        return framework::protocol::BuildErrorResponse(context.request, result.error_code, result.error_message);
    }
    return BuildEnterBattleResponsePacket(context, result);
}

common::net::Packet BattleServerApp::HandleSettleBattleRequest(const framework::protocol::HandlerContext& context,
                                                                const common::net::Packet& packet) const {
    game_backend::proto::SettleBattleRequest request;
    common::net::Packet error_response;
    if (!framework::protocol::ParseProtoRequest(
            context, packet, "invalid settle battle request", &request, &error_response)) {
        return error_response;
    }
    if (!RequestPlayerMatchesContext(context, request.player_id())) {
        return framework::protocol::BuildErrorResponse(
            context.request,
            common::error::ErrorCode::kRequestContextInvalid,
            "request player_id does not match authenticated session");
    }

    auto result = battle_service_->SettleBattle({context.request.player_id,
                                                  request.session_id(),
                                                  request.stage_id(),
                                                  request.star(),
                                                  request.result_code(),
                                                  request.client_score(),
                                                  request.settle_token()},
                                                 context.request.trace_id);
    if (!result.success) {
        return framework::protocol::BuildErrorResponse(context.request, result.error_code, result.error_message);
    }

    return BuildSettleBattleResponsePacket(context, result);
}

common::net::Packet BattleServerApp::HandleGetActiveBattleRequest(const framework::protocol::HandlerContext& context,
                                                                  const common::net::Packet& packet) const {
    game_backend::proto::GetActiveBattleRequest request;
    common::net::Packet error_response;
    if (!framework::protocol::ParseProtoRequest(
            context, packet, "invalid get active battle request", &request, &error_response)) {
        return error_response;
    }
    if (!RequestPlayerMatchesContext(context, request.player_id())) {
        return framework::protocol::BuildErrorResponse(
            context.request,
            common::error::ErrorCode::kRequestContextInvalid,
            "request player_id does not match authenticated session");
    }

    const auto result = battle_service_->GetActiveBattle(context.request.player_id);
    if (!result.success) {
        return framework::protocol::BuildErrorResponse(context.request, result.error_code, result.error_message);
    }
    return BuildGetActiveBattleResponsePacket(context, result);
}

common::net::Packet BattleServerApp::HandleGetRewardGrantStatusRequest(
    const framework::protocol::HandlerContext& context,
    const common::net::Packet& packet) const {
    game_backend::proto::GetRewardGrantStatusRequest request;
    common::net::Packet error_response;
    if (!framework::protocol::ParseProtoRequest(
            context, packet, "invalid get reward grant status request", &request, &error_response)) {
        return error_response;
    }
    if (!RequestPlayerMatchesContext(context, request.player_id())) {
        return framework::protocol::BuildErrorResponse(
            context.request,
            common::error::ErrorCode::kRequestContextInvalid,
            "request player_id does not match authenticated session");
    }

    const auto result = battle_service_->GetRewardGrantStatus(context.request.player_id, request.reward_grant_id());
    if (!result.success) {
        return framework::protocol::BuildErrorResponse(context.request, result.error_code, result.error_message);
    }
    return BuildGetRewardGrantStatusResponsePacket(context, result);
}

}  // namespace services::battle
