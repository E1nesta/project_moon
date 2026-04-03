#pragma once

#include "runtime/foundation/config/simple_config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

struct redisContext;

namespace common::redis {

struct ConnectionOptions {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::string password;
    int database = 0;
    int timeout_ms = 2000;
};

ConnectionOptions ReadConnectionOptions(const config::SimpleConfig& config);

class RedisClient {
public:
    explicit RedisClient(ConnectionOptions options);
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool Connect(std::string* error_message = nullptr);
    [[nodiscard]] bool IsConnected() const;
    bool Ping(std::string* error_message = nullptr);

    bool Set(const std::string& key, const std::string& value, int ttl_seconds = 0, std::string* error_message = nullptr);
    [[nodiscard]] std::optional<std::string> Get(const std::string& key, std::string* error_message = nullptr);
    bool Del(const std::string& key, std::string* error_message = nullptr);
    bool HSet(const std::string& key,
              const std::unordered_map<std::string, std::string>& values,
              int ttl_seconds = 0,
              std::string* error_message = nullptr);
    [[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> HGetAll(const std::string& key,
                                                                                       std::string* error_message = nullptr);
    bool SetNxWithExpire(const std::string& key,
                         const std::string& value,
                         int ttl_seconds,
                         bool* inserted,
                         std::string* error_message = nullptr);
    bool IncrementWithExpire(const std::string& key,
                             int ttl_seconds,
                             std::int64_t* value,
                             std::string* error_message = nullptr);

private:
    [[nodiscard]] std::string LastError() const;

    ConnectionOptions options_;
    redisContext* context_ = nullptr;
};

}  // namespace common::redis
