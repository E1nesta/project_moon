#include "apps/player_internal_grpc_server/player_internal_grpc_server_app.h"

#include "runtime/foundation/build/build_info.h"
#include "runtime/foundation/log/logger.h"
#include "runtime/grpc/server_runner.h"
#include "runtime/transport/service_options.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace services::player {

namespace {

std::atomic_bool g_running{true};

bool UseInternalGrpcMtls(const common::config::SimpleConfig& config) {
    const auto environment = config.GetString("runtime.environment", "local");
    return environment == "staging" || environment == "prod";
}

framework::grpc::TlsServerCredentialsOptions BuildInternalGrpcTlsOptions() {
    framework::grpc::TlsServerCredentialsOptions options;
    options.cert_chain_file = "/etc/game_backend/tls/player_internal_grpc_server.crt";
    options.private_key_file = "/etc/game_backend/tls/player_internal_grpc_server.key";
    options.client_ca_file = "/etc/game_backend/tls/ca.pem";
    options.require_client_auth = true;
    return options;
}

void HandleSignal(int /*signal*/) {
    g_running.store(false);
}

}  // namespace

PlayerInternalGrpcServerApp::PlayerInternalGrpcServerApp(std::string default_service_name, std::string default_config_path)
    : default_service_name_(std::move(default_service_name)),
      default_config_path_(std::move(default_config_path)) {}

int PlayerInternalGrpcServerApp::Main(int argc, char* argv[]) {
    g_running.store(true);
    auto options = framework::runtime::ParseServiceOptions(argc, argv, default_service_name_, default_config_path_);
    if (options.show_version) {
        std::cout << common::build::Version() << '\n';
        return 0;
    }

    auto& logger = common::log::Logger::Instance();
    logger.SetServiceName(options.service_name);
    if (!config_.LoadFromFile(options.config_path)) {
        logger.LogSync(common::log::LogLevel::kError, "failed to load grpc server config");
        return 1;
    }

    logger.SetServiceName(config_.GetString("service.name", options.service_name));
    logger.SetServiceInstanceId(
        config_.GetString("service.instance_id", config_.GetString("service.name", options.service_name)));
    logger.SetEnvironment(config_.GetString("runtime.environment", "local"));
    logger.SetMinLogLevel(config_.GetString("log.level", "info"));
    logger.SetLogFormat(config_.GetString("log.format", "auto"));

    std::string error_message;
    if (!BuildDependencies(&error_message)) {
        logger.LogSync(common::log::LogLevel::kError, error_message);
        return 1;
    }
    if (options.check_only) {
        logger.LogSync(common::log::LogLevel::kInfo, "grpc server configuration and dependencies check passed");
        Shutdown();
        return 0;
    }
    if (!StartServer(&error_message)) {
        logger.LogSync(common::log::LogLevel::kError, error_message);
        Shutdown();
        return 1;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    auto* server_runner = server_runner_.get();
    std::thread shutdown_watcher([server_runner] {
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (server_runner != nullptr) {
            server_runner->Shutdown();
        }
    });

    const auto host = config_.GetString("grpc.listen.host", "0.0.0.0");
    const auto port = config_.GetInt("grpc.listen.port", 7400);
    logger.Log(common::log::LogLevel::kInfo, "player internal grpc server listening on " + host + ":" + std::to_string(port));
    if (server_runner_ != nullptr) {
        server_runner_->Wait();
    }
    g_running.store(false);
    if (shutdown_watcher.joinable()) {
        shutdown_watcher.join();
    }

    Shutdown();
    logger.Flush();
    logger.Shutdown();
    return 0;
}

bool PlayerInternalGrpcServerApp::BuildDependencies(std::string* error_message) {
    mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(config_, "storage.player.mysql."),
        static_cast<std::size_t>(config_.GetInt("storage.player.mysql.pool_size", 4)));
    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(config_, "storage.player.redis."),
        static_cast<std::size_t>(config_.GetInt("storage.player.redis.pool_size", 4)));
    if (!mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    player_repository_ = std::make_unique<game_server::player::MySqlPlayerRepository>(*mysql_pool_);
    player_cache_repository_ = std::make_unique<game_server::player::RedisPlayerCacheRepository>(
        *redis_pool_, config_.GetInt("storage.player.snapshot_ttl_seconds", 300));
    player_service_ = std::make_unique<game_server::player::PlayerService>(*player_repository_, *player_cache_repository_);
    grpc_service_ = std::make_unique<game_server::player::PlayerInternalServiceImpl>(*player_service_);
    return true;
}

bool PlayerInternalGrpcServerApp::StartServer(std::string* error_message) {
    server_runner_ = std::make_unique<framework::grpc::ServerRunner>(
        config_.GetString("grpc.listen.host", "0.0.0.0"),
        config_.GetInt("grpc.listen.port", 7400));
    const auto started = UseInternalGrpcMtls(config_)
                             ? server_runner_->Start(grpc_service_.get(), BuildInternalGrpcTlsOptions(), error_message)
                             : server_runner_->Start(grpc_service_.get(), error_message);
    if (!started) {
        server_runner_.reset();
        return false;
    }
    return true;
}

void PlayerInternalGrpcServerApp::Shutdown() {
    if (server_runner_ != nullptr) {
        server_runner_->Shutdown();
        server_runner_.reset();
    }
    grpc_service_.reset();
    player_service_.reset();
    player_cache_repository_.reset();
    player_repository_.reset();
    redis_pool_.reset();
    mysql_pool_.reset();
}

}  // namespace services::player
