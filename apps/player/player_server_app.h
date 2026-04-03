#pragma once

#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "runtime/transport/service_app.h"
#include "modules/player/infrastructure/mysql_player_repository.h"
#include "modules/player/application/player_service.h"
#include "modules/player/infrastructure/redis_player_cache_repository.h"

#include <memory>

namespace services::player {

// Thin adapter for the player service process.
class PlayerServerApp : public framework::service::ServiceApp {
public:
    PlayerServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    void RegisterRoutes() override;
    bool RequiresTrustedGateway() const override { return true; }

private:
    // Adapter entrypoint: parse request, invoke the player application service and map the response packet.
    common::net::Packet HandleLoadPlayerRequest(const framework::protocol::HandlerContext& context,
                                                const common::net::Packet& packet) const;

    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<game_server::player::MySqlPlayerRepository> player_repository_;
    std::unique_ptr<game_server::player::RedisPlayerCacheRepository> player_cache_repository_;
    std::unique_ptr<game_server::player::PlayerService> player_service_;
};

}  // namespace services::player
