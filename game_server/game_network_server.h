#pragma once

#include "common/config/simple_config.h"
#include "common/mysql/mysql_client.h"
#include "common/net/tcp_server.h"
#include "common/redis/redis_client.h"
#include "game_server/player/mysql_player_repository.h"
#include "game_server/player/player_service.h"
#include "game_server/player/redis_player_cache_repository.h"
#include "login_server/session/redis_session_repository.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace game_server {

class GameNetworkServer {
public:
    explicit GameNetworkServer(common::config::SimpleConfig config);

    bool Initialize(std::string* error_message);
    int Run(const std::function<bool()>& keep_running);

private:
    std::optional<common::net::Packet> HandlePacket(const common::net::IncomingPacket& incoming);

    common::config::SimpleConfig config_;
    common::net::EpollTcpServer server_;
    common::mysql::MySqlClient mysql_client_;
    common::redis::RedisClient redis_client_;
    std::unique_ptr<login_server::session::RedisSessionRepository> session_repository_;
    std::unique_ptr<player::MySqlPlayerRepository> player_repository_;
    std::unique_ptr<player::RedisPlayerCacheRepository> player_cache_repository_;
    std::unique_ptr<player::PlayerService> player_service_;
};

}  // namespace game_server
