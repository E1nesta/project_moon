#pragma once

#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "modules/dungeon/application/dungeon_service.h"
#include "modules/dungeon/infrastructure/grpc_player_snapshot_port.h"
#include "modules/dungeon/infrastructure/in_memory_dungeon_config_repository.h"
#include "modules/dungeon/infrastructure/mysql_dungeon_repository.h"
#include "modules/dungeon/infrastructure/redis_battle_context_repository.h"
#include "modules/dungeon/infrastructure/redis_player_lock_repository.h"
#include "runtime/transport/service_app.h"

#include <memory>

namespace services::dungeon {

// Thin adapter for the dungeon service process.
class DungeonServerApp : public framework::service::ServiceApp {
public:
    DungeonServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    void RegisterRoutes() override;
    bool RequiresTrustedGateway() const override { return true; }

private:
    // Adapter entrypoint for the enter-dungeon use case.
    common::net::Packet HandleEnterDungeonRequest(const framework::protocol::HandlerContext& context,
                                                  const common::net::Packet& packet) const;
    // Adapter entrypoint for the settle-dungeon use case.
    common::net::Packet HandleSettleDungeonRequest(const framework::protocol::HandlerContext& context,
                                                   const common::net::Packet& packet) const;

    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<dungeon_server::dungeon::PlayerSnapshotPort> player_snapshot_port_;
    std::unique_ptr<dungeon_server::dungeon::InMemoryDungeonConfigRepository> dungeon_config_repository_;
    std::unique_ptr<dungeon_server::dungeon::MySqlDungeonRepository> dungeon_repository_;
    std::unique_ptr<dungeon_server::dungeon::RedisBattleContextRepository> battle_context_repository_;
    std::unique_ptr<dungeon_server::dungeon::RedisPlayerLockRepository> player_lock_repository_;
    std::unique_ptr<dungeon_server::dungeon::DungeonService> dungeon_service_;
};

}  // namespace services::dungeon
