#pragma once

#include "runtime/foundation/config/simple_config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct MYSQL;

namespace common::mysql {

struct ConnectionOptions {
    std::string host = "127.0.0.1";
    int port = 3306;
    std::string user = "game";
    std::string password = "gamepass";
    std::string database = "game_backend";
    std::string charset = "utf8mb4";
};

ConnectionOptions ReadConnectionOptions(const config::SimpleConfig& config);

using Row = std::unordered_map<std::string, std::string>;

class MySqlClient {
public:
    explicit MySqlClient(ConnectionOptions options);
    ~MySqlClient();

    MySqlClient(const MySqlClient&) = delete;
    MySqlClient& operator=(const MySqlClient&) = delete;

    bool Connect(std::string* error_message = nullptr);
    [[nodiscard]] bool IsConnected() const;
    bool Ping(std::string* error_message = nullptr);

    [[nodiscard]] std::string Escape(const std::string& value);

    bool Execute(const std::string& sql, std::string* error_message = nullptr, std::uint64_t* affected_rows = nullptr);
    [[nodiscard]] std::vector<Row> Query(const std::string& sql, std::string* error_message = nullptr);
    [[nodiscard]] std::optional<Row> QueryOne(const std::string& sql, std::string* error_message = nullptr);

    bool BeginTransaction(std::string* error_message = nullptr);
    bool Commit(std::string* error_message = nullptr);
    bool Rollback(std::string* error_message = nullptr);

private:
    [[nodiscard]] std::string LastError() const;

    ConnectionOptions options_;
    MYSQL* handle_ = nullptr;
};

}  // namespace common::mysql
