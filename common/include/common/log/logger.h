#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace common::log {

enum class LogLevel {
    kInfo,
    kWarn,
    kError,
};

class Logger {
public:
    static Logger& Instance();

    void SetServiceName(std::string service_name);
    void Log(LogLevel level, std::string_view message);

private:
    Logger() = default;

    [[nodiscard]] std::string LevelToString(LogLevel level) const;

    std::mutex mutex_;
    std::string service_name_ = "service";
};

}  // namespace common::log

