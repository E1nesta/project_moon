#pragma once

#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "modules/battle/application/battle_service.h"
#include "modules/battle/infrastructure/grpc_player_snapshot_port.h"
#include "modules/battle/infrastructure/in_memory_stage_config_repository.h"
#include "modules/battle/infrastructure/mysql_battle_repository.h"
#include "modules/battle/infrastructure/redis_battle_context_repository.h"
#include "modules/battle/infrastructure/redis_player_lock_repository.h"
#include "runtime/transport/service_app.h"

#include <memory>

namespace services::battle {

// Thin adapter for the battle service process.
class BattleServerApp : public framework::service::ServiceApp {
public:
    BattleServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    void RegisterRoutes() override;
    bool RequiresTrustedGateway() const override { return true; }

private:
    common::net::Packet HandleEnterBattleRequest(const framework::protocol::HandlerContext& context,
                                                 const common::net::Packet& packet) const;
    common::net::Packet HandleSettleBattleRequest(const framework::protocol::HandlerContext& context,
                                                  const common::net::Packet& packet) const;
    common::net::Packet HandleGetActiveBattleRequest(const framework::protocol::HandlerContext& context,
                                                     const common::net::Packet& packet) const;
    common::net::Packet HandleGetRewardGrantStatusRequest(const framework::protocol::HandlerContext& context,
                                                          const common::net::Packet& packet) const;

    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<battle_server::battle::PlayerSnapshotPort> player_snapshot_port_;
    std::unique_ptr<battle_server::battle::InMemoryStageConfigRepository> stage_config_repository_;
    std::unique_ptr<battle_server::battle::MySqlBattleRepository> battle_repository_;
    std::unique_ptr<battle_server::battle::RedisBattleContextRepository> battle_context_repository_;
    std::unique_ptr<battle_server::battle::RedisPlayerLockRepository> player_lock_repository_;
    std::unique_ptr<battle_server::battle::BattleService> battle_service_;
};

}  // namespace services::battle
