#include "runtime/storage/mysql/mysql_client.h"

#include <mysql/mysql.h>

#include <memory>

namespace common::mysql {

ConnectionOptions ReadConnectionOptions(const config::SimpleConfig& config) {
    ConnectionOptions options;
    options.host = config.GetString("storage.mysql.host", options.host);
    options.port = config.GetInt("storage.mysql.port", options.port);
    options.user = config.GetString("storage.mysql.user", options.user);
    options.password = config.GetString("storage.mysql.password", options.password);
    options.database = config.GetString("storage.mysql.database", options.database);
    options.charset = config.GetString("storage.mysql.charset", options.charset);
    return options;
}

MySqlClient::MySqlClient(ConnectionOptions options) : options_(std::move(options)) {}

MySqlClient::~MySqlClient() {
    if (handle_ != nullptr) {
        mysql_close(handle_);
        handle_ = nullptr;
    }
}

bool MySqlClient::Connect(std::string* error_message) {
    if (handle_ != nullptr) {
        mysql_close(handle_);
        handle_ = nullptr;
    }

    handle_ = mysql_init(nullptr);
    if (handle_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "mysql_init failed";
        }
        return false;
    }

    mysql_options(handle_, MYSQL_SET_CHARSET_NAME, options_.charset.c_str());

    if (mysql_real_connect(handle_,
                           options_.host.c_str(),
                           options_.user.c_str(),
                           options_.password.c_str(),
                           options_.database.c_str(),
                           static_cast<unsigned int>(options_.port),
                           nullptr,
                           0) == nullptr) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        mysql_close(handle_);
        handle_ = nullptr;
        return false;
    }

    return true;
}

bool MySqlClient::IsConnected() const {
    return handle_ != nullptr;
}

bool MySqlClient::Ping(std::string* error_message) {
    if (handle_ == nullptr && !Connect(error_message)) {
        return false;
    }

    if (mysql_ping(handle_) != 0) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        return false;
    }

    return true;
}

std::string MySqlClient::Escape(const std::string& value) {
    if (handle_ == nullptr) {
        std::string ignored;
        if (!Connect(&ignored)) {
            return value;
        }
    }

    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    const auto length = mysql_real_escape_string(handle_, escaped.data(), value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

bool MySqlClient::Execute(const std::string& sql, std::string* error_message, std::uint64_t* affected_rows) {
    if (!Ping(error_message)) {
        return false;
    }

    if (mysql_query(handle_, sql.c_str()) != 0) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        return false;
    }

    if (mysql_field_count(handle_) > 0) {
        MYSQL_RES* result = mysql_store_result(handle_);
        if (result != nullptr) {
            mysql_free_result(result);
        }
    }

    if (affected_rows != nullptr) {
        *affected_rows = mysql_affected_rows(handle_);
    }

    return true;
}

std::vector<Row> MySqlClient::Query(const std::string& sql, std::string* error_message) {
    std::vector<Row> rows;
    if (!Ping(error_message)) {
        return rows;
    }

    if (mysql_query(handle_, sql.c_str()) != 0) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        return rows;
    }

    using ResultPtr = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;
    ResultPtr result(mysql_store_result(handle_), &mysql_free_result);
    if (!result) {
        return rows;
    }

    const auto field_count = mysql_num_fields(result.get());
    MYSQL_FIELD* fields = mysql_fetch_fields(result.get());

    while (MYSQL_ROW mysql_row = mysql_fetch_row(result.get())) {
        unsigned long* lengths = mysql_fetch_lengths(result.get());
        Row row;
        for (unsigned int index = 0; index < field_count; ++index) {
            const std::string key = fields[index].name == nullptr ? "" : fields[index].name;
            const std::string value = mysql_row[index] == nullptr ? "" : std::string(mysql_row[index], lengths[index]);
            row.emplace(key, value);
        }
        rows.emplace_back(std::move(row));
    }

    return rows;
}

std::optional<Row> MySqlClient::QueryOne(const std::string& sql, std::string* error_message) {
    auto rows = Query(sql, error_message);
    if (rows.empty()) {
        return std::nullopt;
    }
    return rows.front();
}

bool MySqlClient::BeginTransaction(std::string* error_message) {
    return Execute("START TRANSACTION", error_message);
}

bool MySqlClient::Commit(std::string* error_message) {
    return Execute("COMMIT", error_message);
}

bool MySqlClient::Rollback(std::string* error_message) {
    return Execute("ROLLBACK", error_message);
}

std::string MySqlClient::LastError() const {
    if (handle_ == nullptr) {
        return "mysql handle not initialized";
    }
    return mysql_error(handle_);
}

}  // namespace common::mysql
