#include "common/bootstrap/service_app.h"
#include "common/build/build_info.h"
#include "common/config/simple_config.h"
#include "common/log/logger.h"
#include "game_server/game_network_server.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

namespace {

std::atomic_bool g_running{true};

void HandleSignal(int /*signal*/) {
    g_running.store(false);
}

common::bootstrap::ServiceOptions ParseOptions(int argc, char* argv[]) {
    common::bootstrap::ServiceOptions options;
    options.service_name = "game_server";
    options.config_path = "configs/game_server.conf";
    options.check_only = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (arg == "--check") {
            options.check_only = true;
        } else if (arg == "--version") {
            options.show_version = true;
        }
    }

    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto options = ParseOptions(argc, argv);
    if (options.show_version) {
        std::cout << common::build::Version() << '\n';
        return 0;
    }
    if (options.check_only) {
        return common::bootstrap::RunService(options);
    }

    common::config::SimpleConfig config;
    auto& logger = common::log::Logger::Instance();
    if (!config.LoadFromFile(options.config_path)) {
        logger.SetServiceName(options.service_name);
        logger.Log(common::log::LogLevel::kError, "failed to load config file: " + options.config_path);
        return 1;
    }
    logger.SetServiceName(config.GetString("service.name", options.service_name));

    game_server::GameNetworkServer server(config);
    std::string error_message;
    if (!server.Initialize(&error_message)) {
        logger.Log(common::log::LogLevel::kError, "game_server init failed: " + error_message);
        return 1;
    }

    logger.Log(common::log::LogLevel::kInfo, "game_server listening on port " + std::to_string(config.GetInt("port", 7200)));
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    return server.Run([] { return g_running.load(); });
}
