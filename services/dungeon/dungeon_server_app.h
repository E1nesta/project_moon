#pragma once

#include "common/mysql/mysql_client_pool.h"
#include "common/redis/redis_client_pool.h"
#include "dungeon_server/dungeon/dungeon_service.h"
#include "dungeon_server/dungeon/in_memory_dungeon_config_repository.h"
#include "dungeon_server/dungeon/mysql_dungeon_repository.h"
#include "dungeon_server/dungeon/redis_battle_context_repository.h"
#include "dungeon_server/dungeon/redis_player_lock_repository.h"
#include "framework/service/service_app.h"
#include "game_server/player/mysql_player_repository.h"
#include "game_server/player/redis_player_cache_repository.h"
#include "login_server/session/redis_session_repository.h"

#include <memory>

namespace services::dungeon {

// Thin adapter for the dungeon service process.
class DungeonServerApp : public framework::service::ServiceApp {
public:
    DungeonServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    void RegisterRoutes() override;

private:
    // Adapter entrypoint for the enter-dungeon use case.
    common::net::Packet HandleEnterDungeonRequest(const framework::protocol::HandlerContext& context,
                                                  const common::net::Packet& packet) const;
    // Adapter entrypoint for the settle-dungeon use case.
    common::net::Packet HandleSettleDungeonRequest(const framework::protocol::HandlerContext& context,
                                                   const common::net::Packet& packet) const;

    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<login_server::session::RedisSessionRepository> session_repository_;
    std::unique_ptr<game_server::player::MySqlPlayerRepository> player_repository_;
    std::unique_ptr<game_server::player::RedisPlayerCacheRepository> player_cache_repository_;
    std::unique_ptr<dungeon_server::dungeon::InMemoryDungeonConfigRepository> dungeon_config_repository_;
    std::unique_ptr<dungeon_server::dungeon::MySqlDungeonRepository> dungeon_repository_;
    std::unique_ptr<dungeon_server::dungeon::RedisBattleContextRepository> battle_context_repository_;
    std::unique_ptr<dungeon_server::dungeon::RedisPlayerLockRepository> player_lock_repository_;
    std::unique_ptr<dungeon_server::dungeon::DungeonService> dungeon_service_;
};

}  // namespace services::dungeon
