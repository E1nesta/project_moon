#include "apps/dungeon/dungeon_server_app.h"

#include "runtime/grpc/channel_factory.h"
#include "runtime/protocol/proto_mapper.h"
#include "runtime/protocol/adapter_utils.h"

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

common::net::Packet BuildEnterDungeonResponsePacket(
    const framework::protocol::HandlerContext& context,
    const dungeon_server::dungeon::EnterDungeonResponse& result) {
    game_backend::proto::EnterDungeonResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_battle_id(result.battle_id);
    response.set_remain_stamina(result.remain_stamina);
    return common::net::BuildPacket(
        common::net::MessageId::kEnterDungeonResponse, context.request.request_id, response);
}

common::net::Packet BuildSettleDungeonResponsePacket(
    const framework::protocol::HandlerContext& context,
    const dungeon_server::dungeon::SettleDungeonResponse& result) {
    game_backend::proto::SettleDungeonResponse response;
    framework::protocol::FillResponseContext(context.request, &response);
    response.set_first_clear(result.first_clear);
    for (const auto& reward : result.rewards) {
        FillProtoReward(reward, response.add_rewards());
    }
    return common::net::BuildPacket(
        common::net::MessageId::kSettleDungeonResponse, context.request.request_id, response);
}

}  // namespace

DungeonServerApp::DungeonServerApp()
    : framework::service::ServiceApp("dungeon_server", "configs/dungeon_server.conf") {}

bool DungeonServerApp::BuildDependencies(std::string* error_message) {
    mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(Config()),
        static_cast<std::size_t>(Config().GetInt("storage.mysql.pool_size", 4)));
    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config()),
        static_cast<std::size_t>(Config().GetInt("storage.redis.pool_size", 4)));
    if (!mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    player_snapshot_port_ = std::make_unique<dungeon_server::dungeon::GrpcPlayerSnapshotPort>(
        framework::grpc::CreateInsecureChannel(Config(), "grpc.client.player_internal."));
    dungeon_config_repository_ =
        std::make_unique<dungeon_server::dungeon::InMemoryDungeonConfigRepository>(
            dungeon_server::dungeon::InMemoryDungeonConfigRepository::FromConfig(Config()));
    dungeon_repository_ = std::make_unique<dungeon_server::dungeon::MySqlDungeonRepository>(*mysql_pool_);
    battle_context_repository_ = std::make_unique<dungeon_server::dungeon::RedisBattleContextRepository>(
        *redis_pool_, Config().GetInt("storage.battle.context_ttl_seconds", 3600));
    player_lock_repository_ = std::make_unique<dungeon_server::dungeon::RedisPlayerLockRepository>(
        *redis_pool_, Config().GetInt("storage.player.lock_ttl_seconds", 10));
    dungeon_service_ = std::make_unique<dungeon_server::dungeon::DungeonService>(
        *player_lock_repository_,
        *player_snapshot_port_,
        *dungeon_config_repository_,
        *dungeon_repository_,
        *battle_context_repository_);
    return true;
}

void DungeonServerApp::RegisterRoutes() {
    Routes().Register(common::net::MessageId::kEnterDungeonRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleEnterDungeonRequest(context, packet);
                      });

    Routes().Register(common::net::MessageId::kSettleDungeonRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleSettleDungeonRequest(context, packet);
                      });
}

common::net::Packet DungeonServerApp::HandleEnterDungeonRequest(
    const framework::protocol::HandlerContext& context,
    const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::EnterDungeonRequest>(
        context,
        packet,
        "invalid enter dungeon request",
        [this, &context](const game_backend::proto::EnterDungeonRequest& request) {
            return dungeon_service_->EnterDungeon({context.request.player_id, request.dungeon_id()});
        },
        BuildEnterDungeonResponsePacket);
}

common::net::Packet DungeonServerApp::HandleSettleDungeonRequest(
    const framework::protocol::HandlerContext& context,
    const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::SettleDungeonRequest>(
        context,
        packet,
        "invalid settle dungeon request",
        [this, &context](const game_backend::proto::SettleDungeonRequest& request) {
            return dungeon_service_->SettleDungeon(
                {context.request.player_id,
                 request.battle_id(),
                 request.dungeon_id(),
                 request.star(),
                 request.result()});
        },
        BuildSettleDungeonResponsePacket);
}

}  // namespace services::dungeon
