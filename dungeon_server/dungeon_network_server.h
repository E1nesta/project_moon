#pragma once

#include "common/config/simple_config.h"
#include "common/mysql/mysql_client.h"
#include "common/net/tcp_server.h"
#include "common/redis/redis_client.h"
#include "dungeon_server/dungeon/dungeon_service.h"
#include "dungeon_server/dungeon/in_memory_dungeon_config_repository.h"
#include "dungeon_server/dungeon/mysql_dungeon_repository.h"
#include "dungeon_server/dungeon/redis_player_lock_repository.h"
#include "dungeon_server/dungeon/redis_battle_context_repository.h"
#include "game_server/player/mysql_player_repository.h"
#include "game_server/player/redis_player_cache_repository.h"
#include "login_server/session/redis_session_repository.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace dungeon_server {

class DungeonNetworkServer {
public:
    explicit DungeonNetworkServer(common::config::SimpleConfig config);

    bool Initialize(std::string* error_message);
    int Run(const std::function<bool()>& keep_running);

private:
    std::optional<common::net::Packet> HandlePacket(const common::net::IncomingPacket& incoming);

    common::config::SimpleConfig config_;
    common::net::EpollTcpServer server_;
    common::mysql::MySqlClient mysql_client_;
    common::redis::RedisClient redis_client_;
    std::unique_ptr<login_server::session::RedisSessionRepository> session_repository_;
    std::unique_ptr<game_server::player::MySqlPlayerRepository> player_repository_;
    std::unique_ptr<game_server::player::RedisPlayerCacheRepository> player_cache_repository_;
    std::unique_ptr<dungeon::InMemoryDungeonConfigRepository> dungeon_config_repository_;
    std::unique_ptr<dungeon::MySqlDungeonRepository> dungeon_repository_;
    std::unique_ptr<dungeon::RedisBattleContextRepository> battle_context_repository_;
    std::unique_ptr<dungeon::RedisPlayerLockRepository> player_lock_repository_;
    std::unique_ptr<dungeon::DungeonService> dungeon_service_;
};

}  // namespace dungeon_server
