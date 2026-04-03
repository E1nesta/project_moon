#pragma once

#include "modules/player/application/player_service.h"
#include "modules/player/infrastructure/mysql_player_repository.h"
#include "modules/player/infrastructure/redis_player_cache_repository.h"
#include "modules/player/interfaces/grpc/player_internal_service_impl.h"
#include "runtime/foundation/config/simple_config.h"
#include "runtime/grpc/server_runner.h"
#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client_pool.h"

#include <memory>
#include <string>

namespace services::player {

class PlayerInternalGrpcServerApp {
public:
    PlayerInternalGrpcServerApp(std::string default_service_name, std::string default_config_path);

    int Main(int argc, char* argv[]);

private:
    bool BuildDependencies(std::string* error_message);
    bool StartServer(std::string* error_message);
    void Shutdown();

    std::string default_service_name_;
    std::string default_config_path_;
    common::config::SimpleConfig config_;
    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<game_server::player::MySqlPlayerRepository> player_repository_;
    std::unique_ptr<game_server::player::RedisPlayerCacheRepository> player_cache_repository_;
    std::unique_ptr<game_server::player::PlayerService> player_service_;
    std::unique_ptr<game_server::player::PlayerInternalServiceImpl> grpc_service_;
    std::unique_ptr<framework::grpc::ServerRunner> server_runner_;
};

}  // namespace services::player
