#include "apps/battle_reward_worker/battle_reward_worker_app.h"

#include "runtime/foundation/build/build_info.h"
#include "runtime/foundation/log/logger.h"
#include "runtime/grpc/channel_factory.h"
#include "runtime/transport/service_options.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <thread>

namespace services::dungeon {

namespace {

std::atomic_bool g_running{true};

void HandleSignal(int /*signal*/) {
    g_running.store(false);
}

std::vector<common::model::Reward> ParseRewards(const std::string& raw) {
    std::vector<common::model::Reward> rewards;
    std::size_t cursor = 0;
    while (true) {
        const auto type_key = raw.find("\"reward_type\"", cursor);
        if (type_key == std::string::npos) {
            break;
        }
        const auto type_value_begin = raw.find('"', raw.find(':', type_key) + 1);
        if (type_value_begin == std::string::npos) {
            break;
        }
        const auto type_value_end = raw.find('"', type_value_begin + 1);
        if (type_value_end == std::string::npos) {
            break;
        }
        const auto amount_key = raw.find("\"amount\"", type_value_end);
        if (amount_key == std::string::npos) {
            break;
        }
        const auto amount_begin = raw.find_first_of("-0123456789", raw.find(':', amount_key) + 1);
        if (amount_begin == std::string::npos) {
            break;
        }
        const auto amount_end = raw.find_first_not_of("-0123456789", amount_begin);
        rewards.push_back(
            {raw.substr(type_value_begin + 1, type_value_end - type_value_begin - 1),
             std::stoll(raw.substr(amount_begin, amount_end - amount_begin))});
        cursor = amount_end == std::string::npos ? raw.size() : amount_end;
    }
    return rewards;
}

std::optional<std::int64_t> ExtractJsonInt64Field(const std::string& raw, const std::string& field_name) {
    const auto key = "\"" + field_name + "\"";
    const auto key_pos = raw.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto colon_pos = raw.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto value_begin = raw.find_first_of("-0123456789", colon_pos + 1);
    if (value_begin == std::string::npos) {
        return std::nullopt;
    }
    const auto value_end = raw.find_first_not_of("-0123456789", value_begin);
    try {
        return std::stoll(raw.substr(value_begin, value_end - value_begin));
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

BattleRewardWorkerApp::BattleRewardWorkerApp() = default;

int BattleRewardWorkerApp::Main(int argc, char* argv[]) {
    g_running.store(true);
    auto options =
        framework::runtime::ParseServiceOptions(argc, argv, "battle_reward_worker", "configs/battle_reward_worker.conf");
    if (options.show_version) {
        std::cout << common::build::Version() << '\n';
        return 0;
    }

    auto& logger = common::log::Logger::Instance();
    logger.SetServiceName("battle_reward_worker");
    if (!config_.LoadFromFile(options.config_path)) {
        logger.LogSync(common::log::LogLevel::kError, "failed to load battle reward worker config");
        return 1;
    }
    logger.SetServiceName(config_.GetString("service.name", "battle_reward_worker"));
    logger.SetServiceInstanceId(config_.GetString("service.instance_id", "battle_reward_worker"));
    logger.SetEnvironment(config_.GetString("runtime.environment", "local"));
    logger.SetMinLogLevel(config_.GetString("log.level", "info"));
    logger.SetLogFormat(config_.GetString("log.format", "auto"));

    std::string error_message;
    if (!BuildDependencies(&error_message)) {
        logger.LogSync(common::log::LogLevel::kError, error_message);
        return 1;
    }

    if (options.check_only) {
        logger.LogSync(common::log::LogLevel::kInfo, "battle reward worker dependency check passed");
        Shutdown();
        return 0;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    const auto code = RunLoop();
    Shutdown();
    logger.Flush();
    logger.Shutdown();
    return code;
}

bool BattleRewardWorkerApp::BuildDependencies(std::string* error_message) {
    battle_mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(config_, "storage.battle.mysql."),
        static_cast<std::size_t>(config_.GetInt("storage.battle.mysql.pool_size", 4)));
    if (!battle_mysql_pool_->Initialize(error_message)) {
        return false;
    }

    battle_repository_ = std::make_unique<dungeon_server::dungeon::MySqlDungeonRepository>(*battle_mysql_pool_);
    player_snapshot_port_ = std::make_unique<dungeon_server::dungeon::GrpcPlayerSnapshotPort>(
        framework::grpc::CreateInsecureChannel(config_, "grpc.client.player_internal."));
    producer_ = std::make_unique<common::mq::RocketMqProducer>(
        common::mq::ReadRocketMqOptions(config_, "mq.rocketmq."));
    consumer_ = std::make_unique<common::mq::RocketMqConsumer>(
        common::mq::ReadRocketMqOptions(config_, "mq.rocketmq."));
    return true;
}

void BattleRewardWorkerApp::PublishPendingEvents() {
    auto& logger = common::log::Logger::Instance();
    for (const auto& event : battle_repository_->LoadPublishableOutboxEvents(32)) {
        std::string error_message;
        const auto published = producer_->Publish({config_.GetString("mq.rocketmq.topic", "battle.settlement.v1"),
                                                   event.idempotency_key,
                                                   std::to_string(event.player_id == 0 ? event.session_id
                                                                                       : event.player_id),
                                                   event.payload_json},
                                                  &error_message);
        if (published) {
            if (!battle_repository_->MarkOutboxPublished(event.event_id, &error_message)) {
                logger.Log(common::log::LogLevel::kWarn,
                           "battle reward worker mark published failed: event_id=" + std::to_string(event.event_id) +
                               ", error=" + error_message);
            }
        } else {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker publish failed: event_id=" + std::to_string(event.event_id) +
                           ", error=" + error_message);
        }
    }
}

bool BattleRewardWorkerApp::ProcessRewardDelivery(const common::mq::RocketMqDelivery& delivery, int max_retry_count) {
    auto& logger = common::log::Logger::Instance();
    const auto event_id = ExtractJsonInt64Field(delivery.payload, "event_id");
    if (!event_id.has_value()) {
        logger.Log(common::log::LogLevel::kWarn,
                   "battle reward worker received malformed rocketmq payload: message_id=" + delivery.message_id);
        return false;
    }

    const auto event = battle_repository_->FindOutboxEventById(*event_id);
    if (!event.has_value()) {
        logger.Log(common::log::LogLevel::kWarn,
                   "battle reward worker outbox event not found for rocketmq message: event_id=" +
                       std::to_string(*event_id));
        return false;
    }

    std::string error_message;
    if (event->publish_status == 2) {
        if (!consumer_->Ack(delivery, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker ack already consumed message failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        return true;
    }

    const auto status = battle_repository_->GetRewardGrantStatus(event->reward_grant_id);
    if (status.success && status.grant_status != 0) {
        if (!battle_repository_->MarkOutboxConsumed(event->event_id, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker mark duplicated outbox consumed failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        if (!consumer_->Ack(delivery, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker ack duplicated grant message failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        return true;
    }

    const auto rewards = ParseRewards(event->reward_json);
    const auto apply_result = player_snapshot_port_->ApplyRewardGrant(
        event->player_id, event->reward_grant_id, event->session_id, rewards, event->idempotency_key);
    if (apply_result.success) {
        if (!battle_repository_->MarkRewardGrantDone(event->reward_grant_id, apply_result.rewards, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker mark reward grant done failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        if (!battle_repository_->MarkOutboxConsumed(event->event_id, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker mark outbox consumed failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        if (!consumer_->Ack(delivery, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker ack reward grant message failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        return true;
    }

    if (event->retry_count + 1 >= max_retry_count) {
        if (!battle_repository_->MarkRewardGrantFailed(event->reward_grant_id, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker mark reward grant failed failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        if (!battle_repository_->MarkOutboxConsumed(event->event_id, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker mark permanently failed outbox consumed failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        if (!consumer_->Ack(delivery, &error_message)) {
            logger.Log(common::log::LogLevel::kWarn,
                       "battle reward worker ack permanently failed grant message failed: event_id=" +
                           std::to_string(event->event_id) + ", error=" + error_message);
            return false;
        }
        logger.Log(common::log::LogLevel::kWarn,
                   "battle reward worker apply reward grant failed permanently: " + apply_result.error_message);
        return true;
    }

    if (!battle_repository_->ScheduleOutboxRetry(event->event_id, &error_message)) {
        logger.Log(common::log::LogLevel::kWarn,
                   "battle reward worker schedule retry failed: event_id=" + std::to_string(event->event_id) +
                       ", error=" + error_message);
    }
    logger.Log(common::log::LogLevel::kWarn,
               "battle reward worker apply reward grant failed, waiting rocketmq redelivery: " +
                   apply_result.error_message);
    return false;
}

void BattleRewardWorkerApp::ConsumeRewardMessages() {
    auto& logger = common::log::Logger::Instance();
    std::vector<common::mq::RocketMqDelivery> deliveries;
    std::string error_message;
    if (!consumer_->Poll(static_cast<std::size_t>(config_.GetInt("worker.consume_batch_size", 16)),
                         &deliveries,
                         &error_message)) {
        logger.Log(common::log::LogLevel::kWarn, "battle reward worker poll rocketmq failed: " + error_message);
        return;
    }

    const auto max_retry_count = config_.GetInt("worker.max_retry_count", 8);
    for (const auto& delivery : deliveries) {
        ProcessRewardDelivery(delivery, max_retry_count);
    }
}

int BattleRewardWorkerApp::RunLoop() {
    while (g_running.load()) {
        PublishPendingEvents();
        ConsumeRewardMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.GetInt("worker.poll_interval_ms", 500)));
    }
    return 0;
}

void BattleRewardWorkerApp::Shutdown() {
    consumer_.reset();
    producer_.reset();
    player_snapshot_port_.reset();
    battle_repository_.reset();
    battle_mysql_pool_.reset();
}

}  // namespace services::dungeon
