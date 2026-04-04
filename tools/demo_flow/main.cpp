#include "runtime/foundation/config/simple_config.h"
#include "runtime/foundation/error/error_code.h"
#include "runtime/foundation/log/logger.h"
#include "runtime/observability/structured_log.h"
#include "runtime/storage/mysql/mysql_client.h"
#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "modules/dungeon/application/dungeon_service.h"
#include "modules/dungeon/infrastructure/in_memory_dungeon_config_repository.h"
#include "modules/dungeon/infrastructure/mysql_dungeon_repository.h"
#include "modules/dungeon/infrastructure/redis_battle_context_repository.h"
#include "modules/dungeon/infrastructure/redis_player_lock_repository.h"
#include "modules/player/infrastructure/mysql_player_repository.h"
#include "modules/player/application/player_service.h"
#include "modules/player/infrastructure/redis_player_cache_repository.h"
#include "modules/login/infrastructure/mysql_account_repository.h"
#include "modules/login/application/login_service.h"
#include "runtime/session/redis_session_store.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

class LocalPlayerSnapshotPort final : public dungeon_server::dungeon::PlayerSnapshotPort {
public:
    explicit LocalPlayerSnapshotPort(game_server::player::PlayerService& player_service)
        : player_service_(player_service) {}

    std::optional<dungeon_server::dungeon::PlayerSnapshot> LoadPlayerSnapshot(std::int64_t player_id) const override {
        const auto response = player_service_.GetPlayerSnapshot(player_id);
        if (!response.success || !response.found) {
            return std::nullopt;
        }

        dungeon_server::dungeon::PlayerSnapshot snapshot;
        snapshot.player_id = response.player_id;
        snapshot.level = response.level;
        snapshot.stamina = response.stamina;
        return snapshot;
    }

    bool InvalidatePlayerSnapshot(std::int64_t player_id) override {
        return player_service_.InvalidatePlayerCache(player_id).success;
    }

    dungeon_server::dungeon::SpendStaminaResponse SpendStaminaForDungeonEnter(std::int64_t player_id,
                                                                              const std::string& battle_id,
                                                                              int stamina_cost) override {
        const auto response = player_service_.SpendStaminaForDungeonEnter(player_id, battle_id, stamina_cost);
        return {response.success, response.error_code, response.error_message, response.remain_stamina};
    }

    dungeon_server::dungeon::ApplySettlementResponse ApplyDungeonSettlement(std::int64_t player_id,
                                                                            const std::string& battle_id,
                                                                            int dungeon_id,
                                                                            int star,
                                                                            std::int64_t normal_gold_reward,
                                                                            std::int64_t first_clear_diamond_reward) override {
        const auto response = player_service_.ApplyDungeonSettlement(
            player_id, battle_id, dungeon_id, star, normal_gold_reward, first_clear_diamond_reward);
        dungeon_server::dungeon::ApplySettlementResponse result;
        result.success = response.success;
        result.error_code = response.error_code;
        result.error_message = response.error_message;
        result.first_clear = response.first_clear;
        result.rewards.push_back({"gold", response.gold_reward});
        if (response.diamond_reward > 0) {
            result.rewards.push_back({"diamond", response.diamond_reward});
        }
        return result;
    }

private:
    game_server::player::PlayerService& player_service_;
};

struct DemoOptions {
    std::string login_config = "configs/login_server.conf";
    std::string player_config = "configs/player_server.conf";
    std::string dungeon_config = "configs/dungeon_server.conf";
    std::string account_name = "demo";
    std::string password = "demo123";
    bool reset_demo_state = true;
    bool run_negative_cases = true;
};

void EmitToolLog(common::log::LogLevel level, const framework::observability::LogEntry& entry);
framework::observability::LogEntry NewToolEvent(std::string_view event);

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

    auto entry = NewToolEvent("demo_flow_config_load_failed");
    entry.Add("config_path", path);
    entry.Add("message", "failed to load config file");
    EmitToolLog(common::log::LogLevel::kError, entry);
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

void EmitToolLog(common::log::LogLevel level, const framework::observability::LogEntry& entry) {
    common::log::Logger::Instance().Log(level, entry.Build());
}

framework::observability::LogEntry NewToolEvent(std::string_view event) {
    framework::observability::LogEntry entry;
    entry.Add("event", event);
    return entry;
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

    auto entry = NewToolEvent("demo_flow_state_reset_completed");
    entry.Add("account_id", account_id);
    entry.Add("player_id", player_id);
    entry.Add("dungeon_id", static_cast<std::int64_t>(dungeon_id));
    EmitToolLog(common::log::LogLevel::kInfo, entry);
}

bool Require(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    auto entry = NewToolEvent("demo_flow_assertion_failed");
    entry.Add("message", message);
    EmitToolLog(common::log::LogLevel::kError, entry);
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
    auto entry = NewToolEvent("demo_flow_rewards_observed");
    entry.Add("rewards", output.str().substr(std::string("rewards=").size()));
    EmitToolLog(common::log::LogLevel::kInfo, entry);
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
        auto entry = NewToolEvent("demo_flow_dependency_connect_failed");
        entry.Add("dependency", "mysql");
        entry.Add("message", error_message);
        EmitToolLog(common::log::LogLevel::kError, entry);
        return 1;
    }

    common::redis::RedisClient redis_client(common::redis::ReadConnectionOptions(player_config));
    if (!redis_client.Connect(&error_message)) {
        auto entry = NewToolEvent("demo_flow_dependency_connect_failed");
        entry.Add("dependency", "redis");
        entry.Add("message", error_message);
        EmitToolLog(common::log::LogLevel::kError, entry);
        return 1;
    }

    common::mysql::MySqlClientPool mysql_pool(common::mysql::ReadConnectionOptions(player_config), 1);
    if (!mysql_pool.Initialize(&error_message)) {
        auto entry = NewToolEvent("demo_flow_dependency_pool_init_failed");
        entry.Add("dependency", "mysql");
        entry.Add("message", error_message);
        EmitToolLog(common::log::LogLevel::kError, entry);
        return 1;
    }

    common::redis::RedisClientPool redis_pool(common::redis::ReadConnectionOptions(player_config), 1);
    if (!redis_pool.Initialize(&error_message)) {
        auto entry = NewToolEvent("demo_flow_dependency_pool_init_failed");
        entry.Add("dependency", "redis");
        entry.Add("message", error_message);
        EmitToolLog(common::log::LogLevel::kError, entry);
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

    auto session_repository = common::session::RedisSessionStore::FromConfig(redis_pool, login_config);
    login_server::LoginService login_service(account_repository, session_repository);

    game_server::player::MySqlPlayerRepository player_repository(mysql_pool);
    auto player_cache_repository = game_server::player::RedisPlayerCacheRepository::FromConfig(redis_pool, player_config);
    game_server::player::PlayerService player_service(player_repository, player_cache_repository);
    LocalPlayerSnapshotPort player_snapshot_port(player_service);

    auto dungeon_config_repository = dungeon_server::dungeon::InMemoryDungeonConfigRepository::FromConfig(dungeon_config);
    dungeon_server::dungeon::MySqlDungeonRepository dungeon_repository(mysql_pool);
    auto battle_context_repository = dungeon_server::dungeon::RedisBattleContextRepository::FromConfig(redis_pool, dungeon_config);
    dungeon_server::dungeon::RedisPlayerLockRepository player_lock_repository(
        redis_pool, dungeon_config.GetInt("ttl.player_lock_seconds", 10));
    dungeon_server::dungeon::DungeonService dungeon_service(player_lock_repository,
                                                            player_snapshot_port,
                                                            dungeon_config_repository,
                                                            dungeon_repository,
                                                            battle_context_repository);

    const auto login_response = login_service.Login({options.account_name, options.password});
    if (!Require(login_response.success, "login failed: " + FormatError(login_response.error_code, login_response.error_message))) {
        return 1;
    }

    auto login_entry = NewToolEvent("demo_flow_login_succeeded");
    login_entry.Add("account_name", options.account_name);
    login_entry.Add("auth_token", framework::observability::MaskAuthToken(login_response.session.session_id));
    login_entry.Add("account_id", login_response.session.account_id);
    login_entry.Add("player_id", login_response.default_player_id);
    EmitToolLog(common::log::LogLevel::kInfo, login_entry);

    auto player_response = player_service.LoadPlayer(login_response.default_player_id);
    if (!Require(player_response.success,
                 "load player failed: " + FormatError(player_response.error_code, player_response.error_message))) {
        return 1;
    }

    auto player_entry = NewToolEvent("demo_flow_load_player_succeeded");
    player_entry.Add("cache", player_response.loaded_from_cache ? "hit" : "miss");
    player_entry.Add("player_id", player_response.player_state.profile.player_id);
    player_entry.Add("level", static_cast<std::int64_t>(player_response.player_state.profile.level));
    player_entry.Add("stamina", static_cast<std::int64_t>(player_response.player_state.profile.stamina));
    player_entry.Add("gold", player_response.player_state.profile.gold);
    player_entry.Add("diamond", player_response.player_state.profile.diamond);
    EmitToolLog(common::log::LogLevel::kInfo, player_entry);

    player_response = player_service.LoadPlayer(login_response.default_player_id);
    if (!Require(player_response.success && player_response.loaded_from_cache,
                 "second load should hit redis snapshot: " +
                     FormatError(player_response.error_code, player_response.error_message))) {
        return 1;
    }

    const auto dungeon_id = dungeon_config.GetInt("demo.dungeon_id", 1001);
    const auto enter_response = dungeon_service.EnterDungeon(
        {login_response.default_player_id, dungeon_id});
    if (!Require(enter_response.success,
                 "enter dungeon failed: " + FormatError(enter_response.error_code, enter_response.error_message))) {
        return 1;
    }

    auto enter_entry = NewToolEvent("demo_flow_enter_dungeon_succeeded");
    enter_entry.Add("player_id", login_response.default_player_id);
    enter_entry.Add("dungeon_id", static_cast<std::int64_t>(dungeon_id));
    enter_entry.Add("battle_id", enter_response.battle_id);
    enter_entry.Add("remain_stamina", static_cast<std::int64_t>(enter_response.remain_stamina));
    EmitToolLog(common::log::LogLevel::kInfo, enter_entry);

    const auto settle_response =
        dungeon_service.SettleDungeon({login_response.default_player_id,
                                       enter_response.battle_id,
                                       dungeon_id,
                                       3,
                                       true});
    if (!Require(settle_response.success,
                 "settle dungeon failed: " + FormatError(settle_response.error_code, settle_response.error_message))) {
        return 1;
    }

    auto settle_entry = NewToolEvent("demo_flow_settle_dungeon_succeeded");
    settle_entry.Add("player_id", login_response.default_player_id);
    settle_entry.Add("dungeon_id", static_cast<std::int64_t>(dungeon_id));
    settle_entry.Add("battle_id", enter_response.battle_id);
    settle_entry.Add("first_clear", settle_response.first_clear ? "true" : "false");
    EmitToolLog(common::log::LogLevel::kInfo, settle_entry);
    LogRewards(settle_response.rewards);

    const auto refreshed_player = player_service.LoadPlayer(login_response.default_player_id);
    if (!Require(refreshed_player.success,
                 "reload player after settlement failed: " +
                     FormatError(refreshed_player.error_code, refreshed_player.error_message))) {
        return 1;
    }

    auto refreshed_entry = NewToolEvent("demo_flow_reload_player_succeeded");
    refreshed_entry.Add("player_id", login_response.default_player_id);
    refreshed_entry.Add("stamina", static_cast<std::int64_t>(refreshed_player.player_state.profile.stamina));
    refreshed_entry.Add("gold", refreshed_player.player_state.profile.gold);
    refreshed_entry.Add("diamond", refreshed_player.player_state.profile.diamond);
    refreshed_entry.Add("dungeon_progress_count",
                        static_cast<std::int64_t>(refreshed_player.player_state.dungeon_progress.size()));
    EmitToolLog(common::log::LogLevel::kInfo, refreshed_entry);

    if (options.run_negative_cases) {
        const auto wrong_password = login_service.Login({options.account_name, "bad-password"});
        if (!Require(!wrong_password.success && wrong_password.error_code == common::error::ErrorCode::kInvalidPassword,
                     "invalid password case should be rejected")) {
            return 1;
        }

        const auto duplicate_settle =
            dungeon_service.SettleDungeon({login_response.default_player_id,
                                           enter_response.battle_id,
                                           dungeon_id,
                                           3,
                                           true});
        if (!Require(duplicate_settle.success &&
                         duplicate_settle.first_clear == settle_response.first_clear &&
                         duplicate_settle.rewards.size() == settle_response.rewards.size(),
                     "duplicate settlement should replay the original result")) {
            return 1;
        }

        const auto invalid_star_enter = dungeon_service.EnterDungeon(
            {login_response.default_player_id, dungeon_id});
        if (!Require(invalid_star_enter.success,
                     "second enter for invalid-star case should succeed: " +
                         FormatError(invalid_star_enter.error_code, invalid_star_enter.error_message))) {
            return 1;
        }

        const auto invalid_star_settle =
            dungeon_service.SettleDungeon({login_response.default_player_id,
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

    auto done_entry = NewToolEvent("demo_flow_completed");
    done_entry.Add("account_name", options.account_name);
    EmitToolLog(common::log::LogLevel::kInfo, done_entry);
    return 0;
}
