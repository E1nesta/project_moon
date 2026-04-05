#include "runtime/foundation/config/simple_config.h"
#include "runtime/foundation/error/error_code.h"
#include "runtime/foundation/log/logger.h"
#include "runtime/observability/structured_log.h"
#include "runtime/session/redis_session_store.h"
#include "runtime/storage/mysql/mysql_client.h"
#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "modules/battle/application/battle_service.h"
#include "modules/battle/domain/player_snapshot.h"
#include "modules/battle/infrastructure/in_memory_stage_config_repository.h"
#include "modules/battle/infrastructure/mysql_battle_repository.h"
#include "modules/battle/infrastructure/redis_battle_context_repository.h"
#include "modules/battle/infrastructure/redis_player_lock_repository.h"
#include "modules/login/application/login_service.h"
#include "modules/login/infrastructure/mysql_account_repository.h"
#include "modules/player/application/player_service.h"
#include "modules/player/infrastructure/mysql_player_repository.h"
#include "modules/player/infrastructure/redis_player_cache_repository.h"
#include "tools/demo_support.h"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

class LocalPlayerSnapshotPort final : public battle_server::battle::PlayerSnapshotPort {
public:
    explicit LocalPlayerSnapshotPort(game_server::player::PlayerService& player_service)
        : player_service_(player_service) {}

    std::optional<battle_server::battle::PlayerSnapshot> GetBattleEntrySnapshot(std::int64_t player_id) const override {
        const auto response = player_service_.GetBattleEntrySnapshot(player_id);
        if (!response.success || !response.found) {
            return std::nullopt;
        }

        battle_server::battle::PlayerSnapshot snapshot;
        snapshot.player_id = response.player_id;
        snapshot.level = response.level;
        snapshot.stamina = response.energy;
        snapshot.role_summaries.reserve(response.role_summaries.size());
        for (const auto& role_summary : response.role_summaries) {
            snapshot.role_summaries.push_back(
                {role_summary.role_id, role_summary.level, role_summary.star});
        }
        return snapshot;
    }

    bool InvalidatePlayerSnapshot(std::int64_t player_id) override {
        return player_service_.InvalidatePlayerCache(player_id).success;
    }

    battle_server::battle::PrepareBattleEntryPortResponse PrepareBattleEntry(std::int64_t player_id,
                                                                               std::int64_t session_id,
                                                                               int energy_cost,
                                                                               const std::string& idempotency_key) override {
        const auto response = player_service_.PrepareBattleEntry(player_id, session_id, energy_cost, idempotency_key);
        return {response.success, response.error_code, response.error_message, response.remain_energy};
    }

    battle_server::battle::CancelBattleEntryPortResponse CancelBattleEntry(
        std::int64_t player_id,
        std::int64_t session_id,
        int energy_refund,
        const std::string& idempotency_key) override {
        const auto response = player_service_.CancelBattleEntry(player_id, session_id, energy_refund, idempotency_key);
        return {response.success, response.error_code, response.error_message};
    }

    battle_server::battle::ApplyRewardGrantPortResponse ApplyRewardGrant(
        std::int64_t player_id,
        std::int64_t grant_id,
        std::int64_t session_id,
        const std::vector<common::model::Reward>& rewards,
        const std::string& idempotency_key) override {
        const auto response = player_service_.ApplyRewardGrant(player_id, grant_id, session_id, rewards, idempotency_key);
        return {response.success, response.error_code, response.error_message, {}};
    }

private:
    game_server::player::PlayerService& player_service_;
};

struct DemoOptions {
    std::string login_config = "configs/demo/login_server.conf";
    std::string player_config = "configs/demo/player_server.conf";
    std::string stage_config = "configs/demo/battle_server.conf";
    std::string account_name = "demo";
    std::string password = "demo123";
    bool reset_demo_state = true;
    bool run_negative_cases = true;
};

void EmitToolLog(common::log::LogLevel level, const framework::observability::LogEntry& entry) {
    common::log::Logger::Instance().Log(level, entry.Build());
}

framework::observability::LogEntry NewToolEvent(std::string_view event) {
    framework::observability::LogEntry entry;
    entry.Add("event", event);
    return entry;
}

DemoOptions ParseOptions(int argc, char* argv[]) {
    DemoOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--login-config" && index + 1 < argc) {
            options.login_config = argv[++index];
        } else if ((arg == "--player-config" || arg == "--game-config") && index + 1 < argc) {
            options.player_config = argv[++index];
        } else if (arg == "--battle-config" && index + 1 < argc) {
            options.stage_config = argv[++index];
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
    bool first = true;
    for (const auto& reward : rewards) {
        if (!first) {
            output << ';';
        }
        output << reward.reward_type << ':' << reward.amount;
        first = false;
    }
    auto entry = NewToolEvent("demo_flow_rewards_observed");
    entry.Add("rewards", output.str());
    EmitToolLog(common::log::LogLevel::kInfo, entry);
}

bool ConnectMySql(const char* dependency, common::mysql::MySqlClient& client) {
    std::string error_message;
    if (client.Connect(&error_message)) {
        return true;
    }

    auto entry = NewToolEvent("demo_flow_dependency_connect_failed");
    entry.Add("dependency", dependency);
    entry.Add("message", error_message);
    EmitToolLog(common::log::LogLevel::kError, entry);
    return false;
}

bool ConnectRedis(const char* dependency, common::redis::RedisClient& client) {
    std::string error_message;
    if (client.Connect(&error_message)) {
        return true;
    }

    auto entry = NewToolEvent("demo_flow_dependency_connect_failed");
    entry.Add("dependency", dependency);
    entry.Add("message", error_message);
    EmitToolLog(common::log::LogLevel::kError, entry);
    return false;
}

template <typename PoolT>
bool InitializePool(const char* dependency, PoolT& pool) {
    std::string error_message;
    if (pool.Initialize(&error_message)) {
        return true;
    }

    auto entry = NewToolEvent("demo_flow_dependency_pool_init_failed");
    entry.Add("dependency", dependency);
    entry.Add("message", error_message);
    EmitToolLog(common::log::LogLevel::kError, entry);
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    common::log::Logger::Instance().SetServiceName("demo_flow");

    const auto options = ParseOptions(argc, argv);

    common::config::SimpleConfig login_config;
    common::config::SimpleConfig player_config;
    common::config::SimpleConfig stage_config;
    if (!LoadConfig(options.login_config, login_config) ||
        !LoadConfig(options.player_config, player_config) ||
        !LoadConfig(options.stage_config, stage_config)) {
        return 1;
    }

    const auto demo_data = demo::support::ReadDemoDataConfig(login_config, player_config, stage_config);

    common::mysql::MySqlClient account_mysql(common::mysql::ReadConnectionOptions(login_config, "storage.account.mysql."));
    common::mysql::MySqlClient player_mysql(common::mysql::ReadConnectionOptions(player_config, "storage.player.mysql."));
    common::mysql::MySqlClient battle_mysql(common::mysql::ReadConnectionOptions(stage_config, "storage.battle.mysql."));
    common::redis::RedisClient account_redis(common::redis::ReadConnectionOptions(login_config, "storage.account.redis."));
    common::redis::RedisClient player_redis(common::redis::ReadConnectionOptions(player_config, "storage.player.redis."));
    common::redis::RedisClient battle_redis(common::redis::ReadConnectionOptions(stage_config, "storage.battle.redis."));
    if (!ConnectMySql("account_mysql", account_mysql) ||
        !ConnectMySql("player_mysql", player_mysql) ||
        !ConnectMySql("battle_mysql", battle_mysql) ||
        !ConnectRedis("account_redis", account_redis) ||
        !ConnectRedis("player_redis", player_redis) ||
        !ConnectRedis("battle_redis", battle_redis)) {
        return 1;
    }

    std::string error_message;
    if (!demo::support::EnsureDemoData(account_mysql, player_mysql, demo_data, &error_message)) {
        auto entry = NewToolEvent("demo_flow_state_prepare_failed");
        entry.Add("message", error_message);
        EmitToolLog(common::log::LogLevel::kError, entry);
        return 1;
    }
    if (options.reset_demo_state &&
        !demo::support::ResetDemoState(
            player_mysql, battle_mysql, account_redis, player_redis, battle_redis, demo_data, &error_message)) {
        auto entry = NewToolEvent("demo_flow_state_prepare_failed");
        entry.Add("message", error_message);
        EmitToolLog(common::log::LogLevel::kError, entry);
        return 1;
    }

    common::mysql::MySqlClientPool account_mysql_pool(
        common::mysql::ReadConnectionOptions(login_config, "storage.account.mysql."),
        static_cast<std::size_t>(login_config.GetInt("storage.account.mysql.pool_size", 4)));
    common::mysql::MySqlClientPool player_mysql_pool(
        common::mysql::ReadConnectionOptions(login_config, "storage.player.mysql."),
        static_cast<std::size_t>(login_config.GetInt("storage.player.mysql.pool_size", 4)));
    common::mysql::MySqlClientPool battle_mysql_pool(
        common::mysql::ReadConnectionOptions(stage_config, "storage.battle.mysql."),
        static_cast<std::size_t>(stage_config.GetInt("storage.battle.mysql.pool_size", 4)));
    common::redis::RedisClientPool account_redis_pool(
        common::redis::ReadConnectionOptions(login_config, "storage.account.redis."),
        static_cast<std::size_t>(login_config.GetInt("storage.account.redis.pool_size", 4)));
    common::redis::RedisClientPool player_redis_pool(
        common::redis::ReadConnectionOptions(player_config, "storage.player.redis."),
        static_cast<std::size_t>(player_config.GetInt("storage.player.redis.pool_size", 4)));
    common::redis::RedisClientPool battle_redis_pool(
        common::redis::ReadConnectionOptions(stage_config, "storage.battle.redis."),
        static_cast<std::size_t>(stage_config.GetInt("storage.battle.redis.pool_size", 4)));
    if (!InitializePool("account_mysql_pool", account_mysql_pool) ||
        !InitializePool("player_mysql_pool", player_mysql_pool) ||
        !InitializePool("battle_mysql_pool", battle_mysql_pool) ||
        !InitializePool("account_redis_pool", account_redis_pool) ||
        !InitializePool("player_redis_pool", player_redis_pool) ||
        !InitializePool("battle_redis_pool", battle_redis_pool)) {
        return 1;
    }

    login_server::auth::MySqlAccountRepository account_repository(account_mysql_pool, player_mysql_pool);
    auto session_repository = common::session::RedisSessionStore::FromConfig(account_redis_pool, login_config);
    login_server::LoginService login_service(account_repository, session_repository);

    game_server::player::MySqlPlayerRepository player_repository(player_mysql_pool);
    auto player_cache_repository =
        game_server::player::RedisPlayerCacheRepository::FromConfig(player_redis_pool, player_config);
    game_server::player::PlayerService player_service(player_repository, player_cache_repository);
    LocalPlayerSnapshotPort player_snapshot_port(player_service);

    auto stage_config_repository = battle_server::battle::InMemoryStageConfigRepository::FromConfig(stage_config);
    battle_server::battle::MySqlBattleRepository battle_repository(battle_mysql_pool);
    auto battle_context_repository =
        battle_server::battle::RedisBattleContextRepository::FromConfig(battle_redis_pool, stage_config);
    battle_server::battle::RedisPlayerLockRepository player_lock_repository(
        battle_redis_pool, stage_config.GetInt("storage.player.lock_ttl_seconds", 10));
    battle_server::battle::BattleService battle_service(player_lock_repository,
                                                            player_snapshot_port,
                                                            stage_config_repository,
                                                            battle_repository,
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

    const auto initial_stamina = player_response.player_state.profile.stamina;
    auto player_entry = NewToolEvent("demo_flow_load_player_succeeded");
    player_entry.Add("cache", player_response.loaded_from_cache ? "hit" : "miss");
    player_entry.Add("player_id", player_response.player_state.profile.player_id);
    player_entry.Add("level", static_cast<std::int64_t>(player_response.player_state.profile.level));
    player_entry.Add("stamina", static_cast<std::int64_t>(initial_stamina));
    player_entry.Add("gold", player_response.player_state.profile.gold);
    player_entry.Add("diamond", player_response.player_state.profile.diamond);
    EmitToolLog(common::log::LogLevel::kInfo, player_entry);

    player_response = player_service.LoadPlayer(login_response.default_player_id);
    if (!Require(player_response.success && player_response.loaded_from_cache,
                 "second load should hit redis snapshot: " +
                     FormatError(player_response.error_code, player_response.error_message))) {
        return 1;
    }

    const auto enter_response = battle_service.EnterBattle(
        {login_response.default_player_id, demo_data.stage_id, "pve"}, "demo-flow-enter");
    if (!Require(enter_response.success,
                 "enter battle failed: " + FormatError(enter_response.error_code, enter_response.error_message))) {
        return 1;
    }

    auto enter_entry = NewToolEvent("demo_flow_enter_battle_succeeded");
    enter_entry.Add("player_id", login_response.default_player_id);
    enter_entry.Add("stage_id", static_cast<std::int64_t>(demo_data.stage_id));
    enter_entry.Add("session_id", enter_response.session_id);
    enter_entry.Add("remain_stamina", static_cast<std::int64_t>(enter_response.remain_stamina));
    EmitToolLog(common::log::LogLevel::kInfo, enter_entry);

    const auto settle_response = battle_service.SettleBattle(
        {login_response.default_player_id, enter_response.session_id, demo_data.stage_id, 3, 1, 123456},
        "demo-flow-settle");
    if (!Require(settle_response.success,
                 "settle battle failed: " + FormatError(settle_response.error_code, settle_response.error_message))) {
        return 1;
    }

    auto settle_entry = NewToolEvent("demo_flow_settle_battle_succeeded");
    settle_entry.Add("player_id", login_response.default_player_id);
    settle_entry.Add("stage_id", static_cast<std::int64_t>(demo_data.stage_id));
    settle_entry.Add("session_id", enter_response.session_id);
    settle_entry.Add("reward_grant_id", settle_response.reward_grant_id);
    settle_entry.Add("grant_status", static_cast<std::int64_t>(settle_response.grant_status));
    EmitToolLog(common::log::LogLevel::kInfo, settle_entry);
    LogRewards(settle_response.reward_preview);

    const auto grant_status = battle_service.GetRewardGrantStatus(settle_response.reward_grant_id);
    if (!Require(grant_status.success,
                 "get reward grant status failed: " +
                     FormatError(grant_status.error_code, grant_status.error_message))) {
        return 1;
    }

    auto grant_entry = NewToolEvent("demo_flow_reward_grant_status_observed");
    grant_entry.Add("reward_grant_id", grant_status.reward_grant_id);
    grant_entry.Add("grant_status", static_cast<std::int64_t>(grant_status.grant_status));
    EmitToolLog(common::log::LogLevel::kInfo, grant_entry);

    const auto refreshed_player = player_service.LoadPlayer(login_response.default_player_id);
    if (!Require(refreshed_player.success,
                 "reload player failed: " + FormatError(refreshed_player.error_code, refreshed_player.error_message)) ||
        !Require(grant_status.grant_status == 1, "reward grant should be completed synchronously") ||
        !Require(refreshed_player.player_state.profile.stamina == enter_response.remain_stamina,
                 "reload player should reflect stamina consumption") ||
        !Require(refreshed_player.player_state.profile.gold ==
                     player_response.player_state.profile.gold +
                         stage_config.GetInt("demo.stage_normal_gold_reward", 100),
                 "reload player should reflect gold reward") ||
        !Require(refreshed_player.player_state.profile.diamond ==
                     player_response.player_state.profile.diamond +
                         stage_config.GetInt("demo.stage_first_clear_diamond_reward", 50),
                 "reload player should reflect diamond reward")) {
        return 1;
    }

    auto refreshed_entry = NewToolEvent("demo_flow_reload_player_succeeded");
    refreshed_entry.Add("initial_stamina", static_cast<std::int64_t>(initial_stamina));
    refreshed_entry.Add("current_stamina", static_cast<std::int64_t>(refreshed_player.player_state.profile.stamina));
    refreshed_entry.Add("gold", refreshed_player.player_state.profile.gold);
    refreshed_entry.Add("diamond", refreshed_player.player_state.profile.diamond);
    EmitToolLog(common::log::LogLevel::kInfo, refreshed_entry);

    if (options.run_negative_cases) {
        const auto wrong_password = login_service.Login({options.account_name, "bad-password"});
        if (!Require(!wrong_password.success && wrong_password.error_code == common::error::ErrorCode::kInvalidPassword,
                     "invalid password case should be rejected")) {
            return 1;
        }

        const auto duplicate_settle = battle_service.SettleBattle(
            {login_response.default_player_id, enter_response.session_id, demo_data.stage_id, 3, 1, 123456},
            "demo-flow-settle-replay");
        if (!Require(duplicate_settle.success &&
                         duplicate_settle.reward_grant_id == settle_response.reward_grant_id,
                     "duplicate settlement should replay the original result")) {
            return 1;
        }

        const auto invalid_star_enter = battle_service.EnterBattle(
            {login_response.default_player_id, demo_data.stage_id, "pve"}, "demo-flow-invalid-enter");
        if (!Require(invalid_star_enter.success,
                     "second enter battle failed: " +
                         FormatError(invalid_star_enter.error_code, invalid_star_enter.error_message))) {
            return 1;
        }

        const auto invalid_star_settle = battle_service.SettleBattle(
            {login_response.default_player_id, invalid_star_enter.session_id, demo_data.stage_id, 99, 1, 1},
            "demo-flow-invalid-settle");
        if (!Require(!invalid_star_settle.success &&
                         invalid_star_settle.error_code == common::error::ErrorCode::kInvalidStar,
                     "invalid star should be rejected")) {
            return 1;
        }

        const auto cleanup_settle = battle_service.SettleBattle(
            {login_response.default_player_id, invalid_star_enter.session_id, demo_data.stage_id, 1, 1, 321},
            "demo-flow-cleanup-settle");
        if (!Require(cleanup_settle.success,
                     "cleanup settle failed: " +
                         FormatError(cleanup_settle.error_code, cleanup_settle.error_message))) {
            return 1;
        }
    }

    auto done_entry = NewToolEvent("demo_flow_completed");
    done_entry.Add("account_name", options.account_name);
    EmitToolLog(common::log::LogLevel::kInfo, done_entry);
    return 0;
}
