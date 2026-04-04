#include "apps/dungeon/dungeon_server_app.h"

#include "runtime/foundation/log/logger.h"
#include "runtime/grpc/channel_factory.h"
#include "runtime/protocol/adapter_utils.h"
#include "runtime/protocol/proto_mapper.h"

#include "game_backend.pb.h"

namespace services::dungeon {

namespace {

void FillProtoReward(const common::model::Reward& reward, game_backend::proto::Reward* output) {
    if (output == nullptr) {
        return;
    }
    output->set_reward_type(reward.reward_type);
    output->set_amount(reward.amount);
}

common::net::Packet BuildEnterBattleResponsePacket(const framework::protocol::HandlerContext& context,
                                                   const dungeon_server::dungeon::EnterBattleResponse& result) {
    game_backend::proto::EnterBattleResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_session_id(result.session_id);
    response.set_remain_stamina(result.remain_stamina);
    response.set_seed(result.seed);
    return common::net::BuildPacket(common::net::MessageId::kEnterBattleResponse, context.request.request_id, response);
}

common::net::Packet BuildSettleBattleResponsePacket(const framework::protocol::HandlerContext& context,
                                                    const dungeon_server::dungeon::SettleBattleResponse& result) {
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
    const dungeon_server::dungeon::RewardGrantStatusResponse& result) {
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

DungeonServerApp::DungeonServerApp() : framework::service::ServiceApp("dungeon_server", "configs/dungeon_server.conf") {}

bool DungeonServerApp::BuildDependencies(std::string* error_message) {
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

    player_snapshot_port_ = std::make_unique<dungeon_server::dungeon::GrpcPlayerSnapshotPort>(
        framework::grpc::CreateInsecureChannel(Config(), "grpc.client.player_internal."));
    dungeon_config_repository_ = std::make_unique<dungeon_server::dungeon::InMemoryDungeonConfigRepository>(
        dungeon_server::dungeon::InMemoryDungeonConfigRepository::FromConfig(Config()));
    dungeon_repository_ = std::make_unique<dungeon_server::dungeon::MySqlDungeonRepository>(*mysql_pool_);
    battle_context_repository_ = std::make_unique<dungeon_server::dungeon::RedisBattleContextRepository>(
        *redis_pool_, Config().GetInt("storage.battle.context_ttl_seconds", 3600));
    player_lock_repository_ = std::make_unique<dungeon_server::dungeon::RedisPlayerLockRepository>(
        *redis_pool_, Config().GetInt("storage.player.lock_ttl_seconds", 10));
    rocketmq_producer_ = std::make_unique<common::mq::RocketMqProducer>(
        common::mq::ReadRocketMqOptions(Config(), "mq.rocketmq."));
    dungeon_service_ = std::make_unique<dungeon_server::dungeon::DungeonService>(
        *player_lock_repository_,
        *player_snapshot_port_,
        *dungeon_config_repository_,
        *dungeon_repository_,
        *battle_context_repository_);
    return true;
}

void DungeonServerApp::TryPublishSettlementEvent(std::int64_t reward_grant_id) const {
    if (reward_grant_id <= 0 || dungeon_repository_ == nullptr || rocketmq_producer_ == nullptr) {
        return;
    }

    auto& logger = common::log::Logger::Instance();
    const auto event = dungeon_repository_->FindOutboxEventById(reward_grant_id);
    if (!event.has_value()) {
        logger.Log(common::log::LogLevel::kWarn,
                   "battle settlement outbox event not found: reward_grant_id=" + std::to_string(reward_grant_id));
        return;
    }
    if (event->publish_status != 0) {
        return;
    }

    std::string error_message;
    const auto published = rocketmq_producer_->Publish({Config().GetString("mq.rocketmq.topic", "battle.settlement.v1"),
                                                        event->idempotency_key,
                                                        std::to_string(event->player_id == 0 ? event->session_id
                                                                                             : event->player_id),
                                                        event->payload_json},
                                                       &error_message);
    if (!published) {
        logger.Log(common::log::LogLevel::kWarn,
                   "battle settlement rocketmq publish failed: reward_grant_id=" + std::to_string(reward_grant_id) +
                       ", error=" + error_message);
        return;
    }

    if (!dungeon_repository_->MarkOutboxPublished(event->event_id, &error_message)) {
        logger.Log(common::log::LogLevel::kWarn,
                   "battle settlement outbox mark published failed: reward_grant_id=" +
                       std::to_string(reward_grant_id) + ", error=" + error_message);
    }
}

void DungeonServerApp::RegisterRoutes() {
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

common::net::Packet DungeonServerApp::HandleEnterBattleRequest(const framework::protocol::HandlerContext& context,
                                                               const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::EnterBattleRequest>(
        context,
        packet,
        "invalid enter battle request",
        [this, &context](const game_backend::proto::EnterBattleRequest& request) {
            return dungeon_service_->EnterBattle(
                {context.request.player_id, request.stage_id(), request.mode().empty() ? "pve" : request.mode()},
                context.request.trace_id);
        },
        BuildEnterBattleResponsePacket);
}

common::net::Packet DungeonServerApp::HandleSettleBattleRequest(const framework::protocol::HandlerContext& context,
                                                                const common::net::Packet& packet) const {
    game_backend::proto::SettleBattleRequest request;
    common::net::Packet error_response;
    if (!framework::protocol::ParseProtoRequest(
            context, packet, "invalid settle battle request", &request, &error_response)) {
        return error_response;
    }

    auto result = dungeon_service_->SettleBattle({context.request.player_id,
                                                  request.session_id(),
                                                  request.stage_id(),
                                                  request.star(),
                                                  request.result_code(),
                                                  request.client_score()},
                                                 context.request.trace_id);
    if (!result.success) {
        return framework::protocol::BuildErrorResponse(context.request, result.error_code, result.error_message);
    }

    TryPublishSettlementEvent(result.reward_grant_id);
    return BuildSettleBattleResponsePacket(context, result);
}

common::net::Packet DungeonServerApp::HandleGetRewardGrantStatusRequest(
    const framework::protocol::HandlerContext& context,
    const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::GetRewardGrantStatusRequest>(
        context,
        packet,
        "invalid get reward grant status request",
        [this](const game_backend::proto::GetRewardGrantStatusRequest& request) {
            return dungeon_service_->GetRewardGrantStatus(request.reward_grant_id());
        },
        BuildGetRewardGrantStatusResponsePacket);
}

}  // namespace services::dungeon
