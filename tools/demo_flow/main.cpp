#include "common/config/simple_config.h"
#include "common/error/error_code.h"
#include "common/log/logger.h"
#include "common/mysql/mysql_client.h"
#include "common/mysql/mysql_client_pool.h"
#include "common/redis/redis_client.h"
#include "common/redis/redis_client_pool.h"
#include "dungeon_server/dungeon/dungeon_service.h"
#include "dungeon_server/dungeon/in_memory_dungeon_config_repository.h"
#include "dungeon_server/dungeon/mysql_dungeon_repository.h"
#include "dungeon_server/dungeon/redis_battle_context_repository.h"
#include "dungeon_server/dungeon/redis_player_lock_repository.h"
#include "game_server/player/mysql_player_repository.h"
#include "game_server/player/player_service.h"
#include "game_server/player/redis_player_cache_repository.h"
#include "login_server/auth/mysql_account_repository.h"
#include "login_server/login_service.h"
#include "login_server/session/redis_session_repository.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct DemoOptions {
    std::string login_config = "configs/login_server.conf";
    std::string player_config = "configs/player_server.conf";
    std::string dungeon_config = "configs/dungeon_server.conf";
    std::string account_name = "demo";
    std::string password = "demo123";
    bool reset_demo_state = true;
    bool run_negative_cases = true;
};

DemoOptions ParseOptions(int argc, char* argv[]) {
    DemoOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--login-config" && index + 1 < argc) {
            options.login_config = argv[++index];
        } else if ((arg == "--player-config" || arg == "--game-config") && index + 1 < argc) {
            options.player_config = argv[++index];
        } else if (arg == "--dungeon-config" && index + 1 < argc) {
            options.dungeon_config = argv[++index];
        } else if (arg == "--account" && index + 1 < argc) {
            options.account_name = argv[++index];
        } else if (arg == "--password" && index + 1 < argc) {
            options.password = argv[++index];
        } else if (arg == "--no-reset") {
            options.reset_demo_state = false;
        } else if (arg == "--happy-path-only") {
            options.run_negative_cases = false;
        }
    }

    return options;
}

bool LoadConfig(const std::string& path, common::config::SimpleConfig& config) {
    if (config.LoadFromFile(path)) {
        return true;
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kError, "failed to load config file: " + path);
    return false;
}

std::string FormatError(common::error::ErrorCode code, const std::string& message) {
    std::ostringstream output;
    output << "error_code=" << common::error::ToString(code);
    if (!message.empty()) {
        output << ", message=" << message;
    }
    return output.str();
}

void ResetDemoState(common::mysql::MySqlClient& mysql_client,
                    common::redis::RedisClient& redis_client,
                    const common::config::SimpleConfig& player_config,
                    const common::config::SimpleConfig& dungeon_config,
                    std::int64_t account_id,
                    std::int64_t player_id) {
    const auto dungeon_id = dungeon_config.GetInt("demo.dungeon_id", 1001);

    mysql_client.Execute("DELETE FROM reward_log WHERE player_id = " + std::to_string(player_id));
    mysql_client.Execute("DELETE FROM dungeon_battle WHERE player_id = " + std::to_string(player_id));
    mysql_client.Execute("DELETE FROM player_dungeon WHERE player_id = " + std::to_string(player_id) +
                         " AND dungeon_id = " + std::to_string(dungeon_id));

    std::ostringstream asset_sql;
    asset_sql << "UPDATE player_asset SET stamina = " << player_config.GetInt("demo.stamina", 120)
              << ", gold = " << player_config.GetInt("demo.gold", 1000)
              << ", diamond = " << player_config.GetInt("demo.diamond", 100)
              << " WHERE player_id = " << player_id;
    mysql_client.Execute(asset_sql.str());

    const auto active_session_key = "account:session:" + std::to_string(account_id);
    if (const auto active_session = redis_client.Get(active_session_key); active_session.has_value()) {
        redis_client.Del("session:" + *active_session);
    }
    redis_client.Del(active_session_key);
    redis_client.Del("player:snapshot:" + std::to_string(player_id));
    redis_client.Del("player:lock:" + std::to_string(player_id));

    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, "demo state reset complete");
}

bool Require(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kError, message);
    return false;
}

void LogRewards(const std::vector<common::model::Reward>& rewards) {
    std::ostringstream output;
    output << "rewards=";
    bool first = true;
    for (const auto& reward : rewards) {
        if (!first) {
            output << ';';
        }
        output << reward.reward_type << ':' << reward.amount;
        first = false;
    }
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, output.str());
}

}  // namespace

int main(int argc, char* argv[]) {
    common::log::Logger::Instance().SetServiceName("demo_flow");

    const auto options = ParseOptions(argc, argv);

    common::config::SimpleConfig login_config;
    common::config::SimpleConfig player_config;
    common::config::SimpleConfig dungeon_config;
    if (!LoadConfig(options.login_config, login_config) ||
        !LoadConfig(options.player_config, player_config) ||
        !LoadConfig(options.dungeon_config, dungeon_config)) {
        return 1;
    }

    common::mysql::MySqlClient mysql_client(common::mysql::ReadConnectionOptions(player_config));
    std::string error_message;
    if (!mysql_client.Connect(&error_message)) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError, "mysql connect failed: " + error_message);
        return 1;
    }

    common::redis::RedisClient redis_client(common::redis::ReadConnectionOptions(player_config));
    if (!redis_client.Connect(&error_message)) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError, "redis connect failed: " + error_message);
        return 1;
    }

    common::mysql::MySqlClientPool mysql_pool(common::mysql::ReadConnectionOptions(player_config), 1);
    if (!mysql_pool.Initialize(&error_message)) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError, "mysql pool init failed: " + error_message);
        return 1;
    }

    common::redis::RedisClientPool redis_pool(common::redis::ReadConnectionOptions(player_config), 1);
    if (!redis_pool.Initialize(&error_message)) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError, "redis pool init failed: " + error_message);
        return 1;
    }

    login_server::auth::MySqlAccountRepository account_repository(mysql_pool);
    const auto demo_account = account_repository.FindByName(options.account_name);
    if (!Require(demo_account.has_value(), "demo account not found in mysql")) {
        return 1;
    }

    if (options.reset_demo_state) {
        ResetDemoState(
            mysql_client, redis_client, player_config, dungeon_config, demo_account->account_id, demo_account->default_player_id);
    }

    auto session_repository = login_server::session::RedisSessionRepository::FromConfig(redis_pool, login_config);
    login_server::LoginService login_service(account_repository, session_repository);

    game_server::player::MySqlPlayerRepository player_repository(mysql_pool);
    auto player_cache_repository = game_server::player::RedisPlayerCacheRepository::FromConfig(redis_pool, player_config);
    game_server::player::PlayerService player_service(session_repository, player_repository, player_cache_repository);

    auto dungeon_config_repository = dungeon_server::dungeon::InMemoryDungeonConfigRepository::FromConfig(dungeon_config);
    dungeon_server::dungeon::MySqlDungeonRepository dungeon_repository(mysql_pool);
    auto battle_context_repository = dungeon_server::dungeon::RedisBattleContextRepository::FromConfig(redis_pool, dungeon_config);
    dungeon_server::dungeon::RedisPlayerLockRepository player_lock_repository(
        redis_pool, dungeon_config.GetInt("ttl.player_lock_seconds", 10));
    dungeon_server::dungeon::DungeonService dungeon_service(session_repository,
                                                            player_lock_repository,
                                                            player_repository,
                                                            player_cache_repository,
                                                            dungeon_config_repository,
                                                            dungeon_repository,
                                                            battle_context_repository);

    const auto login_response = login_service.Login({options.account_name, options.password});
    if (!Require(login_response.success, "login failed: " + FormatError(login_response.error_code, login_response.error_message))) {
        return 1;
    }

    std::ostringstream login_summary;
    login_summary << "login success"
                  << ", session_id=" << login_response.session.session_id
                  << ", account_id=" << login_response.session.account_id
                  << ", player_id=" << login_response.default_player_id;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, login_summary.str());

    auto player_response = player_service.LoadPlayer(login_response.session.session_id, login_response.default_player_id);
    if (!Require(player_response.success,
                 "load player failed: " + FormatError(player_response.error_code, player_response.error_message))) {
        return 1;
    }

    std::ostringstream player_summary;
    player_summary << "load player success"
                   << ", cache=" << (player_response.loaded_from_cache ? "hit" : "miss")
                   << ", player_id=" << player_response.player_state.profile.player_id
                   << ", level=" << player_response.player_state.profile.level
                   << ", stamina=" << player_response.player_state.profile.stamina
                   << ", gold=" << player_response.player_state.profile.gold
                   << ", diamond=" << player_response.player_state.profile.diamond;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, player_summary.str());

    player_response = player_service.LoadPlayer(login_response.session.session_id, login_response.default_player_id);
    if (!Require(player_response.success && player_response.loaded_from_cache,
                 "second load should hit redis snapshot: " +
                     FormatError(player_response.error_code, player_response.error_message))) {
        return 1;
    }

    const auto dungeon_id = dungeon_config.GetInt("demo.dungeon_id", 1001);
    const auto enter_response = dungeon_service.EnterDungeon(
        {login_response.session.session_id, login_response.default_player_id, dungeon_id});
    if (!Require(enter_response.success,
                 "enter dungeon failed: " + FormatError(enter_response.error_code, enter_response.error_message))) {
        return 1;
    }

    std::ostringstream enter_summary;
    enter_summary << "enter dungeon success"
                  << ", battle_id=" << enter_response.battle_id
                  << ", remain_stamina=" << enter_response.remain_stamina;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, enter_summary.str());

    const auto settle_response =
        dungeon_service.SettleDungeon({login_response.session.session_id,
                                       login_response.default_player_id,
                                       enter_response.battle_id,
                                       dungeon_id,
                                       3,
                                       true});
    if (!Require(settle_response.success,
                 "settle dungeon failed: " + FormatError(settle_response.error_code, settle_response.error_message))) {
        return 1;
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo,
                                        "settle dungeon success, first_clear=" +
                                            std::string(settle_response.first_clear ? "true" : "false"));
    LogRewards(settle_response.rewards);

    const auto refreshed_player = player_service.LoadPlayer(login_response.session.session_id, login_response.default_player_id);
    if (!Require(refreshed_player.success,
                 "reload player after settlement failed: " +
                     FormatError(refreshed_player.error_code, refreshed_player.error_message))) {
        return 1;
    }

    std::ostringstream refreshed_summary;
    refreshed_summary << "reload player success"
                      << ", stamina=" << refreshed_player.player_state.profile.stamina
                      << ", gold=" << refreshed_player.player_state.profile.gold
                      << ", diamond=" << refreshed_player.player_state.profile.diamond
                      << ", dungeon_progress_count=" << refreshed_player.player_state.dungeon_progress.size();
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, refreshed_summary.str());

    if (options.run_negative_cases) {
        const auto wrong_password = login_service.Login({options.account_name, "bad-password"});
        if (!Require(!wrong_password.success && wrong_password.error_code == common::error::ErrorCode::kInvalidPassword,
                     "invalid password case should be rejected")) {
            return 1;
        }

        const auto invalid_session = player_service.LoadPlayer("invalid-session", login_response.default_player_id);
        if (!Require(!invalid_session.success &&
                         invalid_session.error_code == common::error::ErrorCode::kSessionInvalid,
                     "invalid session case should be rejected")) {
            return 1;
        }

        const auto duplicate_settle =
            dungeon_service.SettleDungeon({login_response.session.session_id,
                                           login_response.default_player_id,
                                           enter_response.battle_id,
                                           dungeon_id,
                                           3,
                                           true});
        if (!Require(!duplicate_settle.success &&
                         duplicate_settle.error_code == common::error::ErrorCode::kBattleAlreadySettled,
                     "duplicate settlement should be rejected")) {
            return 1;
        }

        const auto invalid_star_enter = dungeon_service.EnterDungeon(
            {login_response.session.session_id, login_response.default_player_id, dungeon_id});
        if (!Require(invalid_star_enter.success,
                     "second enter for invalid-star case should succeed: " +
                         FormatError(invalid_star_enter.error_code, invalid_star_enter.error_message))) {
            return 1;
        }

        const auto invalid_star_settle =
            dungeon_service.SettleDungeon({login_response.session.session_id,
                                           login_response.default_player_id,
                                           invalid_star_enter.battle_id,
                                           dungeon_id,
                                           99,
                                           true});
        if (!Require(!invalid_star_settle.success &&
                         invalid_star_settle.error_code == common::error::ErrorCode::kInvalidStar,
                     "invalid star should be rejected")) {
            return 1;
        }
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, "demo flow completed successfully");
    return 0;
}
