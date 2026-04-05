#include "tools/demo_support.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace demo::support {

namespace {

constexpr std::array<int, 3> kDefaultRoleIds = {1001, 1002, 1003};
constexpr char kHexDigits[] = "0123456789abcdef";

bool ExecuteSql(common::mysql::MySqlClient& mysql, const std::string& sql, std::string* error_message) {
    return mysql.Execute(sql, error_message);
}

std::optional<std::string> DecodeHex(std::string_view value) {
    if ((value.size() % 2U) != 0U) {
        return std::nullopt;
    }

    auto decode_digit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };

    std::string decoded;
    decoded.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const auto high = decode_digit(value[index]);
        const auto low = decode_digit(value[index + 1U]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    return decoded;
}

std::optional<std::string> ExtractSalt(std::string_view encoded_hash) {
    const auto first = encoded_hash.find('$');
    const auto second = encoded_hash.find('$', first == std::string_view::npos ? first : first + 1);
    const auto third = encoded_hash.find('$', second == std::string_view::npos ? second : second + 1);
    if (first == std::string_view::npos || second == std::string_view::npos || third == std::string_view::npos) {
        return std::nullopt;
    }
    return DecodeHex(encoded_hash.substr(second + 1, third - second - 1));
}

std::string ProfileTable(std::int64_t player_id) {
    return "player_profile_" + PlayerShardSuffix(player_id);
}

std::string CurrencyTable(std::int64_t player_id) {
    return "player_currency_" + PlayerShardSuffix(player_id);
}

std::string CurrencyTxnTable(std::int64_t player_id) {
    return "currency_txn_" + PlayerShardSuffix(player_id);
}

std::string RoleTable(std::int64_t player_id) {
    return "player_role_" + PlayerShardSuffix(player_id);
}

std::string SessionTable() {
    return "battle_session_" + CurrentMonthSuffix();
}

std::string TeamSnapshotTable() {
    return "battle_team_snapshot_" + CurrentMonthSuffix();
}

std::string ResultTable() {
    return "battle_result_" + CurrentMonthSuffix();
}

std::string RewardGrantTable() {
    return "reward_grant_" + CurrentMonthSuffix();
}

bool EnsureAccount(common::mysql::MySqlClient& account_mysql,
                   const DemoDataConfig& config,
                   std::string* error_message) {
    std::ostringstream sql;
    sql << "INSERT INTO account (account_id, account_name, password_hash, salt, status, register_channel, "
           "register_time, last_login_time, created_at, updated_at) VALUES ("
        << config.account_id << ", '" << account_mysql.Escape(config.account_name) << "', '"
        << account_mysql.Escape(config.password_hash) << "', '" << account_mysql.Escape(config.password_salt)
        << "', 1, 'demo', CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3)) "
           "ON DUPLICATE KEY UPDATE account_name = VALUES(account_name), password_hash = VALUES(password_hash), "
           "salt = VALUES(salt), status = 1, updated_at = CURRENT_TIMESTAMP(3)";
    return ExecuteSql(account_mysql, sql.str(), error_message);
}

bool EnsureProfile(common::mysql::MySqlClient& player_mysql,
                   const DemoDataConfig& config,
                   std::string* error_message) {
    std::ostringstream sql;
    sql << "INSERT INTO " << ProfileTable(config.player_id)
        << " (player_id, account_id, server_id, nickname, level, exp, energy, stamina_recover_at, main_stage_id, "
           "fight_power, created_at, updated_at) VALUES ("
        << config.player_id << ", " << config.account_id << ", " << config.server_id << ", '"
        << player_mysql.Escape(config.player_name) << "', " << config.level << ", 0, " << config.stamina
        << ", NULL, " << config.main_stage_id << ", " << config.fight_power
        << ", CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3)) "
           "ON DUPLICATE KEY UPDATE account_id = VALUES(account_id), server_id = VALUES(server_id), "
           "nickname = VALUES(nickname), updated_at = CURRENT_TIMESTAMP(3)";
    return ExecuteSql(player_mysql, sql.str(), error_message);
}

bool EnsureCurrencies(common::mysql::MySqlClient& player_mysql,
                      const DemoDataConfig& config,
                      std::string* error_message) {
    std::ostringstream sql;
    sql << "INSERT INTO " << CurrencyTable(config.player_id)
        << " (player_id, currency_type, amount, version, updated_at) VALUES "
        << "(" << config.player_id << ", 'gold', " << config.gold << ", 1, CURRENT_TIMESTAMP(3)), "
        << "(" << config.player_id << ", 'diamond', " << config.diamond << ", 1, CURRENT_TIMESTAMP(3)) "
           "ON DUPLICATE KEY UPDATE updated_at = updated_at";
    return ExecuteSql(player_mysql, sql.str(), error_message);
}

bool EnsureRoles(common::mysql::MySqlClient& player_mysql,
                 const DemoDataConfig& config,
                 std::string* error_message) {
    std::ostringstream sql;
    sql << "INSERT IGNORE INTO " << RoleTable(config.player_id)
        << " (player_id, role_id, level, star, breakthrough, skill_state_json, equip_state_json, updated_at) VALUES ";
    for (std::size_t index = 0; index < kDefaultRoleIds.size(); ++index) {
        if (index > 0) {
            sql << ", ";
        }
        sql << "(" << config.player_id << ", " << kDefaultRoleIds[index] << ", " << config.level
            << ", 1, 0, NULL, NULL, CURRENT_TIMESTAMP(3))";
    }
    return ExecuteSql(player_mysql, sql.str(), error_message);
}

bool ResetPlayerState(common::mysql::MySqlClient& player_mysql,
                      const DemoDataConfig& config,
                      std::string* error_message) {
    if (!ExecuteSql(player_mysql,
                    "DELETE FROM " + CurrencyTxnTable(config.player_id) + " WHERE player_id = " +
                        std::to_string(config.player_id),
                    error_message)) {
        return false;
    }
    if (!ExecuteSql(player_mysql,
                    "DELETE FROM " + RoleTable(config.player_id) + " WHERE player_id = " +
                        std::to_string(config.player_id),
                    error_message)) {
        return false;
    }
    if (!ExecuteSql(player_mysql,
                    "DELETE FROM " + CurrencyTable(config.player_id) + " WHERE player_id = " +
                        std::to_string(config.player_id),
                    error_message)) {
        return false;
    }

    std::ostringstream profile_sql;
    profile_sql << "INSERT INTO " << ProfileTable(config.player_id)
                << " (player_id, account_id, server_id, nickname, level, exp, energy, stamina_recover_at, "
                   "main_stage_id, fight_power, created_at, updated_at) VALUES ("
                << config.player_id << ", " << config.account_id << ", " << config.server_id << ", '"
                << player_mysql.Escape(config.player_name) << "', " << config.level << ", 0, " << config.stamina
                << ", NULL, " << config.main_stage_id << ", " << config.fight_power
                << ", CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3)) "
                   "ON DUPLICATE KEY UPDATE account_id = VALUES(account_id), server_id = VALUES(server_id), "
                   "nickname = VALUES(nickname), level = VALUES(level), exp = VALUES(exp), energy = VALUES(energy), "
                   "stamina_recover_at = VALUES(stamina_recover_at), main_stage_id = VALUES(main_stage_id), "
                   "fight_power = VALUES(fight_power), updated_at = CURRENT_TIMESTAMP(3)";
    if (!ExecuteSql(player_mysql, profile_sql.str(), error_message)) {
        return false;
    }

    std::ostringstream currency_sql;
    currency_sql << "INSERT INTO " << CurrencyTable(config.player_id)
                 << " (player_id, currency_type, amount, version, updated_at) VALUES "
                 << "(" << config.player_id << ", 'gold', " << config.gold << ", 1, CURRENT_TIMESTAMP(3)), "
                 << "(" << config.player_id << ", 'diamond', " << config.diamond
                 << ", 1, CURRENT_TIMESTAMP(3))";
    if (!ExecuteSql(player_mysql, currency_sql.str(), error_message)) {
        return false;
    }

    std::ostringstream role_sql;
    role_sql << "INSERT INTO " << RoleTable(config.player_id)
             << " (player_id, role_id, level, star, breakthrough, skill_state_json, equip_state_json, updated_at) VALUES ";
    for (std::size_t index = 0; index < kDefaultRoleIds.size(); ++index) {
        if (index > 0) {
            role_sql << ", ";
        }
        role_sql << "(" << config.player_id << ", " << kDefaultRoleIds[index] << ", " << config.level
                 << ", 1, 0, NULL, NULL, CURRENT_TIMESTAMP(3))";
    }
    return ExecuteSql(player_mysql, role_sql.str(), error_message);
}

}  // namespace

DemoDataConfig ReadDemoDataConfig(const common::config::SimpleConfig& login_config,
                                  const common::config::SimpleConfig& player_config,
                                  const common::config::SimpleConfig& stage_config) {
    DemoDataConfig config;
    config.account_id = login_config.GetInt("demo.account_id", 10001);
    config.player_id = player_config.GetInt("demo.player_id", login_config.GetInt("demo.default_player_id", 20001));
    config.account_name = login_config.GetString("demo.account_name", "demo");
    config.player_name = player_config.GetString("demo.player_name", "hero_demo");
    config.password_hash = login_config.GetString(
        "demo.password_hash",
        "pbkdf2_sha256$100000$737461727465722d64656d6f2d73616c74$6f24c15d816cb5760860dcdf75b60d557913253234b3e59620bc715c864c88e2");
    if (const auto salt = ExtractSalt(config.password_hash); salt.has_value()) {
        config.password_salt = *salt;
    }
    config.level = player_config.GetInt("demo.level", 10);
    config.stamina = player_config.GetInt("demo.stamina", 120);
    config.gold = player_config.GetInt("demo.gold", 1000);
    config.diamond = player_config.GetInt("demo.diamond", 100);
    config.stage_id = stage_config.GetInt("demo.stage_id", 1001);
    config.main_stage_id = player_config.GetInt("demo.main_stage_id", config.stage_id);
    config.fight_power = player_config.GetInt("demo.fight_power", std::max(1200, config.level * 100));
    config.server_id = player_config.GetInt("demo.server_id", 1);
    return config;
}

std::string PlayerShardSuffix(std::int64_t player_id) {
    const auto shard = static_cast<int>(player_id & 0x0F);
    std::string suffix = "00";
    suffix[0] = kHexDigits[(shard >> 4) & 0x0F];
    suffix[1] = kHexDigits[shard & 0x0F];
    return suffix;
}

std::string CurrentMonthSuffix() {
    const auto now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[7];
    std::strftime(buffer, sizeof(buffer), "%Y%m", &tm);
    return buffer;
}

bool EnsureDemoData(common::mysql::MySqlClient& account_mysql,
                    common::mysql::MySqlClient& player_mysql,
                    const DemoDataConfig& config,
                    std::string* error_message) {
    if (error_message != nullptr) {
        error_message->clear();
    }
    if (!EnsureAccount(account_mysql, config, error_message)) {
        return false;
    }
    if (!EnsureProfile(player_mysql, config, error_message)) {
        return false;
    }
    if (!EnsureCurrencies(player_mysql, config, error_message)) {
        return false;
    }
    return EnsureRoles(player_mysql, config, error_message);
}

bool ResetDemoState(common::mysql::MySqlClient& player_mysql,
                    common::mysql::MySqlClient& battle_mysql,
                    common::redis::RedisClient& account_redis,
                    common::redis::RedisClient& player_redis,
                    common::redis::RedisClient& battle_redis,
                    const DemoDataConfig& config,
                    std::string* error_message) {
    if (error_message != nullptr) {
        error_message->clear();
    }
    std::vector<std::int64_t> session_ids;
    {
        const auto rows = battle_mysql.Query(
            "SELECT session_id FROM " + SessionTable() + " WHERE player_id = " + std::to_string(config.player_id),
            error_message);
        if (error_message != nullptr && !error_message->empty()) {
            return false;
        }
        for (const auto& row : rows) {
            if (const auto iter = row.find("session_id"); iter != row.end() && !iter->second.empty()) {
                session_ids.push_back(std::stoll(iter->second));
            }
        }
    }

    if (!ExecuteSql(battle_mysql,
                    "DELETE FROM " + TeamSnapshotTable() + " WHERE session_id IN (SELECT session_id FROM " +
                        SessionTable() + " WHERE player_id = " + std::to_string(config.player_id) + ")",
                    error_message)) {
        return false;
    }
    if (!ExecuteSql(battle_mysql,
                    "DELETE FROM " + ResultTable() + " WHERE player_id = " + std::to_string(config.player_id),
                    error_message)) {
        return false;
    }
    if (!ExecuteSql(battle_mysql,
                    "DELETE FROM " + RewardGrantTable() + " WHERE player_id = " + std::to_string(config.player_id),
                    error_message)) {
        return false;
    }
    if (!ExecuteSql(battle_mysql,
                    "DELETE FROM " + SessionTable() + " WHERE player_id = " + std::to_string(config.player_id),
                    error_message)) {
        return false;
    }
    if (!ResetPlayerState(player_mysql, config, error_message)) {
        return false;
    }

    const auto account_session_key = "account:session:" + std::to_string(config.account_id);
    if (const auto active_session = account_redis.Get(account_session_key, error_message); active_session.has_value()) {
        if (!account_redis.Del("session:" + *active_session, error_message)) {
            return false;
        }
    } else if (error_message != nullptr && !error_message->empty()) {
        return false;
    }

    if (!account_redis.Del(account_session_key, error_message)) {
        return false;
    }
    if (!player_redis.Del("player:snapshot:" + std::to_string(config.player_id), error_message)) {
        return false;
    }
    if (!battle_redis.Del("player:lock:" + std::to_string(config.player_id), error_message)) {
        return false;
    }

    for (const auto session_id : session_ids) {
        if (!battle_redis.Del("battle:ctx:" + std::to_string(session_id), error_message)) {
            return false;
        }
    }
    return true;
}

}  // namespace demo::support
