#pragma once

#include "runtime/foundation/config/simple_config.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace common::mq {

struct RocketMqOptions {
    std::string nameserver = "127.0.0.1:9876";
    std::string endpoints = "127.0.0.1:8081";
    std::string topic = "battle_settlement_v1";
    std::string producer_group = "battle_outbox_publisher_v1";
    std::string consumer_group = "battle_reward_worker_v1";
    std::string resource_namespace;
    std::string tag = "*";
    int request_timeout_ms = 3000;
    int long_polling_timeout_ms = 3000;
    int invisible_duration_ms = 30000;
    bool tls = false;
    bool enabled = false;
};

struct RocketMqMessage {
    std::string topic;
    std::string key;
    std::string sharding_key;
    std::string payload;
};

struct RocketMqDelivery {
    std::string topic;
    std::string key;
    std::string message_id;
    std::string receipt_handle;
    std::string payload;
};

RocketMqOptions ReadRocketMqOptions(const common::config::SimpleConfig& config, const std::string& prefix);

class RocketMqProducer {
public:
    explicit RocketMqProducer(RocketMqOptions options);
    ~RocketMqProducer();

    [[nodiscard]] bool Publish(const RocketMqMessage& message, std::string* error_message = nullptr);

private:
    struct Impl;
    RocketMqOptions options_;
    std::unique_ptr<Impl> impl_;
};

class RocketMqConsumer {
public:
    explicit RocketMqConsumer(RocketMqOptions options);
    ~RocketMqConsumer();

    [[nodiscard]] bool Enabled() const { return options_.enabled; }
    [[nodiscard]] const RocketMqOptions& Options() const { return options_; }
    [[nodiscard]] bool Poll(std::size_t max_messages,
                            std::vector<RocketMqDelivery>* deliveries,
                            std::string* error_message = nullptr);
    [[nodiscard]] bool Ack(const RocketMqDelivery& delivery, std::string* error_message = nullptr);

private:
    struct Impl;
    RocketMqOptions options_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace common::mq
