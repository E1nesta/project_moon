#include "common/build/build_info.h"
#include "common/config/simple_config.h"
#include "common/mysql/mysql_client.h"
#include "common/redis/redis_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

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

    common::config::SimpleConfig config;
    if (!config.LoadFromFile(options.config_path)) {
        std::cerr << "failed to load config file: " << options.config_path << '\n';
        return 1;
    }

    if (options.mode == "config") {
        return 0;
    }

    std::string error_message;
    if (config.Contains("mysql.host")) {
        common::mysql::MySqlClient mysql_client(common::mysql::ReadConnectionOptions(config));
        if (!mysql_client.Ping(&error_message)) {
            std::cerr << "mysql not ready: " << error_message << '\n';
            return 1;
        }
    }

    if (config.Contains("redis.host")) {
        common::redis::RedisClient redis_client(common::redis::ReadConnectionOptions(config));
        if (!redis_client.Ping(&error_message)) {
            std::cerr << "redis not ready: " << error_message << '\n';
            return 1;
        }
    }

    const auto timeout_ms = config.GetInt("upstream.timeout_ms", config.GetInt("client.timeout_ms", 2000));
    if (!CheckOptionalTcpDependency(config, "login.upstream.host", "login.upstream.port", timeout_ms, &error_message)) {
        std::cerr << "login upstream not ready: " << error_message << '\n';
        return 1;
    }
    if (!CheckOptionalTcpDependency(config, "game.upstream.host", "game.upstream.port", timeout_ms, &error_message)) {
        std::cerr << "game upstream not ready: " << error_message << '\n';
        return 1;
    }
    if (!CheckOptionalTcpDependency(config, "dungeon.upstream.host", "dungeon.upstream.port", timeout_ms, &error_message)) {
        std::cerr << "dungeon upstream not ready: " << error_message << '\n';
        return 1;
    }

    return 0;
}
