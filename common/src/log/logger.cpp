#include "common/log/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace common::log {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetServiceName(std::string service_name) {
    std::lock_guard lock(mutex_);
    service_name_ = std::move(service_name);
}

void Logger::Log(LogLevel level, std::string_view message) {
    std::lock_guard lock(mutex_);

    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &timestamp);
#else
    localtime_r(&timestamp, &local_time);
#endif

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
           << " [" << service_name_ << "]"
           << " [" << LevelToString(level) << "] "
           << message;

    std::cout << output.str() << std::endl;
}

std::string Logger::LevelToString(LogLevel level) const {
    switch (level) {
    case LogLevel::kInfo:
        return "INFO";
    case LogLevel::kWarn:
        return "WARN";
    case LogLevel::kError:
        return "ERROR";
    }

    return "UNKNOWN";
}

}  // namespace common::log

