#include "apps/battle/battle_server_app.h"

#include "runtime/foundation/log/logger.h"
#include "runtime/grpc/channel_factory.h"
#include "runtime/protocol/adapter_utils.h"
#include "runtime/protocol/proto_mapper.h"

#include "game_backend.pb.h"

namespace services::battle {

namespace {

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
        *battle_context_repository_);
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
    Routes().Register(common::net::MessageId::kGetRewardGrantStatusRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleGetRewardGrantStatusRequest(context, packet);
                      });
}

common::net::Packet BattleServerApp::HandleEnterBattleRequest(const framework::protocol::HandlerContext& context,
                                                               const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::EnterBattleRequest>(
        context,
        packet,
        "invalid enter battle request",
        [this, &context](const game_backend::proto::EnterBattleRequest& request) {
            return battle_service_->EnterBattle(
                {context.request.player_id, request.stage_id(), request.mode().empty() ? "pve" : request.mode()},
                context.request.trace_id);
        },
        BuildEnterBattleResponsePacket);
}

common::net::Packet BattleServerApp::HandleSettleBattleRequest(const framework::protocol::HandlerContext& context,
                                                                const common::net::Packet& packet) const {
    game_backend::proto::SettleBattleRequest request;
    common::net::Packet error_response;
    if (!framework::protocol::ParseProtoRequest(
            context, packet, "invalid settle battle request", &request, &error_response)) {
        return error_response;
    }

    auto result = battle_service_->SettleBattle({context.request.player_id,
                                                  request.session_id(),
                                                  request.stage_id(),
                                                  request.star(),
                                                  request.result_code(),
                                                  request.client_score()},
                                                 context.request.trace_id);
    if (!result.success) {
        return framework::protocol::BuildErrorResponse(context.request, result.error_code, result.error_message);
    }

    return BuildSettleBattleResponsePacket(context, result);
}

common::net::Packet BattleServerApp::HandleGetRewardGrantStatusRequest(
    const framework::protocol::HandlerContext& context,
    const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::GetRewardGrantStatusRequest>(
        context,
        packet,
        "invalid get reward grant status request",
        [this](const game_backend::proto::GetRewardGrantStatusRequest& request) {
            return battle_service_->GetRewardGrantStatus(request.reward_grant_id());
        },
        BuildGetRewardGrantStatusResponsePacket);
}

}  // namespace services::battle
