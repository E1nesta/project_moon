#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "runtime/storage/mysql/mysql_client.h"
#include "runtime/storage/redis/redis_client.h"

#include <cstdint>
#include <string>

namespace demo::support {

struct DemoDataConfig {
    std::int64_t account_id = 10001;
    std::int64_t player_id = 20001;
    std::string account_name = "demo";
    std::string player_name = "hero_demo";
    std::string password_hash;
    std::string password_salt = "starter-demo-salt";
    int server_id = 1;
    int level = 10;
    int stamina = 120;
    int main_progress = 1001;
    std::int64_t fight_power = 1200;
    std::int64_t gold = 1000;
    std::int64_t diamond = 100;
    int stage_id = 1001;
};

DemoDataConfig ReadDemoDataConfig(const common::config::SimpleConfig& login_config,
                                  const common::config::SimpleConfig& player_config,
                                  const common::config::SimpleConfig& dungeon_config);

std::string PlayerShardSuffix(std::int64_t player_id);
std::string CurrentMonthSuffix();

bool EnsureDemoData(common::mysql::MySqlClient& account_mysql,
                    common::mysql::MySqlClient& player_mysql,
                    const DemoDataConfig& config,
                    std::string* error_message = nullptr);

bool ResetDemoState(common::mysql::MySqlClient& player_mysql,
                    common::mysql::MySqlClient& battle_mysql,
                    common::redis::RedisClient& account_redis,
                    common::redis::RedisClient& player_redis,
                    common::redis::RedisClient& battle_redis,
                    const DemoDataConfig& config,
                    std::string* error_message = nullptr);

}  // namespace demo::support
