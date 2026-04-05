#include "runtime/foundation/build/build_info.h"
#include "runtime/foundation/config/simple_config.h"
#include "runtime/storage/mysql/mysql_client.h"
#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client.h"
#include "runtime/storage/redis/redis_client_pool.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct Options {
    std::string config_path;
    std::string mode = "ready";
    bool show_version = false;
};

Options ParseOptions(int argc, char* argv[]) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (arg == "--mode" && index + 1 < argc) {
            options.mode = argv[++index];
        } else if (arg == "--version") {
            options.show_version = true;
        }
    }

    return options;
}

bool CheckTcpDependency(const std::string& host, int port, int timeout_ms, std::string* error_message) {
    (void)timeout_ms;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const auto port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &results) != 0) {
        if (error_message != nullptr) {
            *error_message = "getaddrinfo failed for " + host + ':' + port_text;
        }
        return false;
    }

    bool connected = false;
    for (auto* result = results; result != nullptr; result = result->ai_next) {
        const int socket_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }

        if (connect(socket_fd, result->ai_addr, result->ai_addrlen) == 0) {
            connected = true;
            close(socket_fd);
            break;
        }

        close(socket_fd);
    }

    freeaddrinfo(results);
    if (!connected && error_message != nullptr) {
        *error_message = "connect failed to " + host + ':' + port_text;
    }
    return connected;
}

bool CheckOptionalTcpDependency(const common::config::SimpleConfig& config,
                                const std::string& host_key,
                                const std::string& port_key,
                                int timeout_ms,
                                std::string* error_message) {
    if (!config.Contains(host_key) || !config.Contains(port_key)) {
        return true;
    }

    return CheckTcpDependency(config.GetString(host_key), config.GetInt(port_key), timeout_ms, error_message);
}

bool CheckExecutionConfig(const common::config::SimpleConfig& config, std::string* error_message) {
    const auto worker_threads = config.GetInt("execution.worker_threads", config.GetInt("transport.io_threads", 1));
    const auto shard_count = config.GetInt("execution.shard_count", worker_threads);
    if (worker_threads <= 0) {
        if (error_message != nullptr) {
            *error_message = "execution.worker_threads must be > 0";
        }
        return false;
    }

    if (shard_count != worker_threads) {
        if (error_message != nullptr) {
            *error_message = "execution.shard_count must equal execution.worker_threads";
        }
        return false;
    }

    if (config.GetInt("execution.queue_limit", 1024) <= 0) {
        if (error_message != nullptr) {
            *error_message = "execution.queue_limit must be > 0";
        }
        return false;
    }

    if (config.Contains("gateway.forward_workers") && config.GetInt("gateway.forward_workers", 0) <= 0) {
        if (error_message != nullptr) {
            *error_message = "gateway.forward_workers must be > 0";
        }
        return false;
    }

    if (config.Contains("gateway.forward_queue_limit") && config.GetInt("gateway.forward_queue_limit", 0) <= 0) {
        if (error_message != nullptr) {
            *error_message = "gateway.forward_queue_limit must be > 0";
        }
        return false;
    }

    return true;
}

bool ValidateTlsConfig(const common::config::SimpleConfig& config,
                       const std::string& prefix,
                       bool require_cert_and_key,
                       std::string* error_message) {
    if (!config.GetBool(prefix + "enabled", false)) {
        return true;
    }

    const auto cert_file = config.GetString(prefix + "cert_file");
    const auto key_file = config.GetString(prefix + "key_file");
    const auto ca_file = config.GetString(prefix + "ca_file");
    const auto verify_peer = config.GetBool(prefix + "verify_peer", false);

    if (require_cert_and_key) {
        if (cert_file.empty() || key_file.empty()) {
            if (error_message != nullptr) {
                *error_message = prefix + "cert_file and key_file are required when TLS is enabled";
            }
            return false;
        }
        if (!std::filesystem::exists(cert_file) || !std::filesystem::exists(key_file)) {
            if (error_message != nullptr) {
                *error_message = prefix + "cert_file or key_file does not exist";
            }
            return false;
        }
    }

    if (verify_peer && ca_file.empty()) {
        if (error_message != nullptr) {
            *error_message = prefix + "ca_file is required when verify_peer is enabled";
        }
        return false;
    }

    if (!ca_file.empty() && !std::filesystem::exists(ca_file)) {
        if (error_message != nullptr) {
            *error_message = prefix + "ca_file does not exist";
        }
        return false;
    }

    return true;
}

bool IsSupportedMode(const std::string& mode) {
    return mode == "config" || mode == "live" || mode == "ready";
}

bool ValidateProductionSecrets(const common::config::SimpleConfig& config, std::string* error_message) {
    const auto environment = config.GetString("runtime.environment", "local");
    if (environment != "prod") {
        return true;
    }

    if (config.Contains("storage.mysql.password")) {
        const auto password = config.GetString("storage.mysql.password");
        if (password.empty() || password == "gamepass") {
            if (error_message != nullptr) {
                *error_message = "prod requires non-default storage.mysql.password";
            }
            return false;
        }
    }

    if (config.Contains("storage.redis.host")) {
        const auto password = config.GetString("storage.redis.password");
        if (password.empty()) {
            if (error_message != nullptr) {
                *error_message = "prod requires non-empty storage.redis.password";
            }
            return false;
        }
    }

    return true;
}

bool ValidateTrustedGatewayConfig(const common::config::SimpleConfig& config, std::string* error_message) {
    const auto service_name = config.GetString("service.name");
    const bool requires_trusted_gateway = service_name == "gateway_server" || service_name == "login_server" ||
                                          service_name == "player_server" || service_name == "battle_server";
    if (!requires_trusted_gateway) {
        return true;
    }

    if (config.GetString("security.trusted_gateway.shared_secret").empty()) {
        if (error_message != nullptr) {
            *error_message = "security.trusted_gateway.shared_secret is required";
        }
        return false;
    }

    if (config.GetInt("security.trusted_gateway.max_clock_skew_ms", 10000) <= 0) {
        if (error_message != nullptr) {
            *error_message = "security.trusted_gateway.max_clock_skew_ms must be > 0";
        }
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto options = ParseOptions(argc, argv);
    if (options.show_version) {
        std::cout << common::build::Version() << '\n';
        return 0;
    }

    if (options.config_path.empty()) {
        std::cerr << "missing --config\n";
        return 1;
    }
    if (!IsSupportedMode(options.mode)) {
        std::cerr << "unsupported --mode: " << options.mode << '\n';
        return 1;
    }

    common::config::SimpleConfig config;
    if (!config.LoadFromFile(options.config_path)) {
        std::cerr << "failed to load config file: " << options.config_path << '\n';
        return 1;
    }

    std::string error_message;
    if (!ValidateProductionSecrets(config, &error_message)) {
        std::cerr << "config invalid: " << error_message << '\n';
        return 1;
    }

    if (!ValidateTrustedGatewayConfig(config, &error_message)) {
        std::cerr << "trusted gateway config invalid: " << error_message << '\n';
        return 1;
    }

    if (!CheckExecutionConfig(config, &error_message)) {
        std::cerr << "execution config invalid: " << error_message << '\n';
        return 1;
    }

    if (!ValidateTlsConfig(config, "transport.tls.", true, &error_message) ||
        !ValidateTlsConfig(config, "client.tls.", false, &error_message) ||
        !ValidateTlsConfig(config, "upstream.tls.", false, &error_message) ||
        !ValidateTlsConfig(config, "upstream.login.tls.", false, &error_message) ||
        !ValidateTlsConfig(config, "upstream.player.tls.", false, &error_message) ||
        !ValidateTlsConfig(config, "upstream.battle.tls.", false, &error_message)) {
        std::cerr << "tls config invalid: " << error_message << '\n';
        return 1;
    }

    if (options.mode == "config" || options.mode == "live") {
        return 0;
    }

    if (config.Contains("storage.mysql.host")) {
        common::mysql::MySqlClientPool mysql_pool(
            common::mysql::ReadConnectionOptions(config),
            static_cast<std::size_t>(config.GetInt("storage.mysql.pool_size", 1)));
        if (!mysql_pool.Initialize(&error_message)) {
            std::cerr << "mysql not ready: " << error_message << '\n';
            return 1;
        }
    }

    if (config.Contains("storage.redis.host")) {
        common::redis::RedisClientPool redis_pool(
            common::redis::ReadConnectionOptions(config),
            static_cast<std::size_t>(config.GetInt("storage.redis.pool_size", 1)));
        if (!redis_pool.Initialize(&error_message)) {
            std::cerr << "redis not ready: " << error_message << '\n';
            return 1;
        }
    }

    const auto timeout_ms = config.GetInt("upstream.timeout_ms", config.GetInt("client.timeout_ms", 2000));
    if (!CheckOptionalTcpDependency(config, "upstream.login.host", "upstream.login.port", timeout_ms, &error_message)) {
        std::cerr << "login upstream not ready: " << error_message << '\n';
        return 1;
    }
    if (!CheckOptionalTcpDependency(config, "upstream.player.host", "upstream.player.port", timeout_ms, &error_message)) {
        std::cerr << "player upstream not ready: " << error_message << '\n';
        return 1;
    }
    if (!CheckOptionalTcpDependency(config, "upstream.battle.host", "upstream.battle.port", timeout_ms, &error_message)) {
        std::cerr << "battle upstream not ready: " << error_message << '\n';
        return 1;
    }

    return 0;
}
