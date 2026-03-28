#include "common/bootstrap/service_app.h"

#include "common/config/simple_config.h"
#include "common/log/logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <sstream>
#include <thread>

namespace common::bootstrap {

namespace {

std::atomic_bool g_running{true};

void HandleSignal(int /*signal*/) {
    g_running.store(false);
}

void LogConfigSummary(const config::SimpleConfig& config) {
    auto& logger = log::Logger::Instance();
    std::ostringstream summary;
    summary << "config loaded";

    const auto host = config.GetString("host");
    const auto port = config.GetInt("port", 0);
    if (!host.empty()) {
        summary << ", host=" << host;
    }
    if (port > 0) {
        summary << ", port=" << port;
    }

    const auto mysql_host = config.GetString("mysql.host");
    const auto redis_host = config.GetString("redis.host");
    if (!mysql_host.empty()) {
        summary << ", mysql.host=" << mysql_host;
    }
    if (!redis_host.empty()) {
        summary << ", redis.host=" << redis_host;
    }

    logger.Log(log::LogLevel::kInfo, summary.str());
}

}  // namespace

int RunService(const ServiceOptions& options) {
    config::SimpleConfig config;
    auto& logger = log::Logger::Instance();
    logger.SetServiceName(options.service_name);

    if (!config.LoadFromFile(options.config_path)) {
        logger.Log(log::LogLevel::kError, "failed to load config file: " + options.config_path);
        return 1;
    }

    logger.Log(log::LogLevel::kInfo, "service bootstrap start");
    LogConfigSummary(config);

    if (options.check_only) {
        logger.Log(log::LogLevel::kInfo, "configuration check passed");
        return 0;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    const int heartbeat_ms = config.GetInt("heartbeat_interval_ms", 5000);
    logger.Log(log::LogLevel::kInfo, "service loop started, press Ctrl+C to stop");

    while (g_running.load()) {
        logger.Log(log::LogLevel::kInfo, "service heartbeat");
        std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_ms));
    }

    logger.Log(log::LogLevel::kInfo, "service shutdown complete");
    return 0;
}

}  // namespace common::bootstrap

