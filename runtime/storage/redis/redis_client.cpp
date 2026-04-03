#include "runtime/storage/redis/redis_client.h"

#include <hiredis/hiredis.h>

#include <memory>

namespace common::redis {

ConnectionOptions ReadConnectionOptions(const config::SimpleConfig& config) {
    ConnectionOptions options;
    options.host = config.GetString("storage.redis.host", options.host);
    options.port = config.GetInt("storage.redis.port", options.port);
    options.password = config.GetString("storage.redis.password", options.password);
    options.database = config.GetInt("storage.redis.database", options.database);
    options.timeout_ms = config.GetInt("storage.redis.timeout_ms", options.timeout_ms);
    return options;
}

RedisClient::RedisClient(ConnectionOptions options) : options_(std::move(options)) {}

RedisClient::~RedisClient() {
    if (context_ != nullptr) {
        redisFree(context_);
        context_ = nullptr;
    }
}

bool RedisClient::Connect(std::string* error_message) {
    if (context_ != nullptr) {
        redisFree(context_);
        context_ = nullptr;
    }

    timeval timeout{};
    timeout.tv_sec = options_.timeout_ms / 1000;
    timeout.tv_usec = (options_.timeout_ms % 1000) * 1000;
    context_ = redisConnectWithTimeout(options_.host.c_str(), options_.port, timeout);

    if (context_ == nullptr || context_->err != 0) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        if (context_ != nullptr) {
            redisFree(context_);
            context_ = nullptr;
        }
        return false;
    }

    if (!options_.password.empty()) {
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
            static_cast<redisReply*>(redisCommand(context_, "AUTH %b", options_.password.data(), options_.password.size())),
            &freeReplyObject);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (error_message != nullptr) {
                *error_message = reply ? reply->str : LastError();
            }
            return false;
        }
    }

    if (options_.database > 0) {
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
            static_cast<redisReply*>(redisCommand(context_, "SELECT %d", options_.database)),
            &freeReplyObject);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (error_message != nullptr) {
                *error_message = reply ? reply->str : LastError();
            }
            return false;
        }
    }

    return true;
}

bool RedisClient::IsConnected() const {
    return context_ != nullptr && context_->err == 0;
}

bool RedisClient::Ping(std::string* error_message) {
    if (!IsConnected() && !Connect(error_message)) {
        return false;
    }

    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommand(context_, "PING")),
        &freeReplyObject);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (error_message != nullptr) {
            *error_message = reply ? reply->str : LastError();
        }
        return false;
    }

    return true;
}

bool RedisClient::Set(const std::string& key, const std::string& value, int ttl_seconds, std::string* error_message) {
    if (!Ping(error_message)) {
        return false;
    }

    redisReply* raw_reply = nullptr;
    if (ttl_seconds > 0) {
        raw_reply = static_cast<redisReply*>(redisCommand(
            context_, "SET %b %b EX %d", key.data(), key.size(), value.data(), value.size(), ttl_seconds));
    } else {
        raw_reply = static_cast<redisReply*>(
            redisCommand(context_, "SET %b %b", key.data(), key.size(), value.data(), value.size()));
    }

    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(raw_reply, &freeReplyObject);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (error_message != nullptr) {
            *error_message = reply ? reply->str : LastError();
        }
        return false;
    }

    return true;
}

std::optional<std::string> RedisClient::Get(const std::string& key, std::string* error_message) {
    if (!Ping(error_message)) {
        return std::nullopt;
    }

    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommand(context_, "GET %b", key.data(), key.size())),
        &freeReplyObject);
    if (!reply) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        return std::nullopt;
    }

    if (reply->type == REDIS_REPLY_NIL) {
        return std::nullopt;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        if (error_message != nullptr) {
            *error_message = reply->str;
        }
        return std::nullopt;
    }

    return std::string(reply->str, reply->len);
}

bool RedisClient::Del(const std::string& key, std::string* error_message) {
    if (!Ping(error_message)) {
        return false;
    }

    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommand(context_, "DEL %b", key.data(), key.size())),
        &freeReplyObject);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (error_message != nullptr) {
            *error_message = reply ? reply->str : LastError();
        }
        return false;
    }

    return true;
}

bool RedisClient::HSet(const std::string& key,
                       const std::unordered_map<std::string, std::string>& values,
                       int ttl_seconds,
                       std::string* error_message) {
    if (!Ping(error_message)) {
        return false;
    }

    for (const auto& [field, value] : values) {
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
            static_cast<redisReply*>(redisCommand(
                context_, "HSET %b %b %b", key.data(), key.size(), field.data(), field.size(), value.data(), value.size())),
            &freeReplyObject);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (error_message != nullptr) {
                *error_message = reply ? reply->str : LastError();
            }
            return false;
        }
    }

    if (ttl_seconds > 0) {
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
            static_cast<redisReply*>(redisCommand(context_, "EXPIRE %b %d", key.data(), key.size(), ttl_seconds)),
            &freeReplyObject);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (error_message != nullptr) {
                *error_message = reply ? reply->str : LastError();
            }
            return false;
        }
    }

    return true;
}

std::optional<std::unordered_map<std::string, std::string>> RedisClient::HGetAll(const std::string& key,
                                                                                  std::string* error_message) {
    if (!Ping(error_message)) {
        return std::nullopt;
    }

    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommand(context_, "HGETALL %b", key.data(), key.size())),
        &freeReplyObject);
    if (!reply) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        return std::nullopt;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        if (error_message != nullptr) {
            *error_message = reply->str;
        }
        return std::nullopt;
    }

    if (reply->type != REDIS_REPLY_ARRAY || reply->elements == 0) {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> values;
    for (size_t index = 0; index + 1 < reply->elements; index += 2) {
        const auto* field = reply->element[index];
        const auto* value = reply->element[index + 1];
        if (field == nullptr || value == nullptr || field->str == nullptr || value->str == nullptr) {
            continue;
        }
        values.emplace(std::string(field->str, field->len), std::string(value->str, value->len));
    }

    return values;
}

bool RedisClient::SetNxWithExpire(const std::string& key,
                                  const std::string& value,
                                  int ttl_seconds,
                                  bool* inserted,
                                  std::string* error_message) {
    if (!Ping(error_message)) {
        return false;
    }

    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommand(
            context_, "SET %b %b NX EX %d", key.data(), key.size(), value.data(), value.size(), ttl_seconds)),
        &freeReplyObject);
    if (!reply) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        return false;
    }

    if (reply->type == REDIS_REPLY_NIL) {
        if (inserted != nullptr) {
            *inserted = false;
        }
        return true;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        if (error_message != nullptr) {
            *error_message = reply->str;
        }
        return false;
    }

    if (inserted != nullptr) {
        *inserted = true;
    }
    return true;
}

bool RedisClient::IncrementWithExpire(const std::string& key,
                                      int ttl_seconds,
                                      std::int64_t* value,
                                      std::string* error_message) {
    if (!Ping(error_message)) {
        return false;
    }

    std::unique_ptr<redisReply, decltype(&freeReplyObject)> increment_reply(
        static_cast<redisReply*>(redisCommand(context_, "INCR %b", key.data(), key.size())),
        &freeReplyObject);
    if (!increment_reply) {
        if (error_message != nullptr) {
            *error_message = LastError();
        }
        return false;
    }

    if (increment_reply->type == REDIS_REPLY_ERROR) {
        if (error_message != nullptr) {
            *error_message = increment_reply->str;
        }
        return false;
    }

    if (increment_reply->type != REDIS_REPLY_INTEGER) {
        if (error_message != nullptr) {
            *error_message = "redis INCR returned unexpected reply";
        }
        return false;
    }

    if (value != nullptr) {
        *value = increment_reply->integer;
    }

    if (increment_reply->integer == 1 && ttl_seconds > 0) {
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> expire_reply(
            static_cast<redisReply*>(redisCommand(context_, "EXPIRE %b %d", key.data(), key.size(), ttl_seconds)),
            &freeReplyObject);
        if (!expire_reply || expire_reply->type == REDIS_REPLY_ERROR) {
            if (error_message != nullptr) {
                *error_message = expire_reply ? expire_reply->str : LastError();
            }
            return false;
        }
    }

    return true;
}

std::string RedisClient::LastError() const {
    if (context_ == nullptr) {
        return "redis context not initialized";
    }

    if (context_->errstr == nullptr) {
        return "redis unknown error";
    }

    return context_->errstr;
}

}  // namespace common::redis
