#pragma once

#include "common/mysql/mysql_client_pool.h"
#include "common/redis/redis_client_pool.h"
#include "framework/service/service_app.h"
#include "game_server/player/mysql_player_repository.h"
#include "game_server/player/player_service.h"
#include "game_server/player/redis_player_cache_repository.h"
#include "login_server/session/redis_session_repository.h"

#include <memory>

namespace services::player {

// Thin adapter for the player service process.
class PlayerServerApp : public framework::service::ServiceApp {
public:
    PlayerServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    void RegisterRoutes() override;

private:
    // Adapter entrypoint: parse request, invoke the player application service and map the response packet.
    common::net::Packet HandleLoadPlayerRequest(const framework::protocol::HandlerContext& context,
                                                const common::net::Packet& packet) const;

    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<login_server::session::RedisSessionRepository> session_repository_;
    std::unique_ptr<game_server::player::MySqlPlayerRepository> player_repository_;
    std::unique_ptr<game_server::player::RedisPlayerCacheRepository> player_cache_repository_;
    std::unique_ptr<game_server::player::PlayerService> player_service_;
};

}  // namespace services::player
