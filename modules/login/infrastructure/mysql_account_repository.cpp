#include "modules/login/infrastructure/mysql_account_repository.h"

#include <chrono>
#include <sstream>

namespace login_server::auth {

namespace {

std::string ShardSuffix(int shard_no) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string suffix = "00";
    suffix[0] = kHexDigits[(shard_no >> 4) & 0x0F];
    suffix[1] = kHexDigits[shard_no & 0x0F];
    return suffix;
}

}  // namespace

MySqlAccountRepository::MySqlAccountRepository(common::mysql::MySqlClientPool& account_mysql_pool,
                                               common::mysql::MySqlClientPool& player_mysql_pool)
    : mysql_pool_(&account_mysql_pool), player_mysql_pool_(&player_mysql_pool) {}

MySqlAccountRepository::MySqlAccountRepository(common::mysql::MySqlClient& mysql_client) : mysql_client_(&mysql_client) {}

std::optional<common::model::Account> MySqlAccountRepository::FindByName(const std::string& account_name) const {
    auto mysql_lease = mysql_pool_ != nullptr ? std::optional<common::mysql::MySqlClientPool::Lease>(mysql_pool_->Acquire())
                                              : std::nullopt;
    auto* mysql = mysql_lease.has_value() ? mysql_lease->operator->() : mysql_client_;
    std::ostringstream sql;
    sql << "SELECT a.account_id, a.account_name, a.password_hash, a.status, "
           "COALESCE(rs.realname_status, 1) AS realname_status, "
           "EXISTS(SELECT 1 FROM ban_record br WHERE br.account_id = a.account_id AND br.ban_scope = 'login' "
           "AND (br.end_time IS NULL OR br.end_time > CURRENT_TIMESTAMP(3))) AS login_banned "
           "FROM account a "
           "LEFT JOIN realname_state rs ON rs.account_id = a.account_id "
           "WHERE a.account_name = '"
        << mysql->Escape(account_name)
        << "' LIMIT 1";

    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    common::model::Account account;
    account.account_id = std::stoll(row->at("account_id"));
    account.account_name = row->at("account_name");
    account.password_hash = row->at("password_hash");
    account.default_player_id = FindDefaultPlayerId(account.account_id);
    account.enabled = row->at("status") == "1";
    account.login_banned = row->at("login_banned") == "1";
    account.realname_verified = row->at("realname_status") == "1";
    return account;
}

bool MySqlAccountRepository::RecordLoginAudit(std::int64_t account_id,
                                              bool success,
                                              const std::string& trace_id,
                                              std::string* error_message) {
    auto mysql_lease = mysql_pool_ != nullptr ? std::optional<common::mysql::MySqlClientPool::Lease>(mysql_pool_->Acquire())
                                              : std::nullopt;
    auto* mysql = mysql_lease.has_value() ? mysql_lease->operator->() : mysql_client_;
    std::ostringstream sql;
    sql << "INSERT INTO login_audit (audit_id, account_id, login_time, login_result, channel, risk_score, "
           "risk_reason_digest, created_at) VALUES ("
        << (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
        << ", " << account_id << ", CURRENT_TIMESTAMP(3), " << (success ? 1 : 0)
        << ", 'password', 0, '" << mysql->Escape(trace_id) << "', CURRENT_TIMESTAMP(3))";
    return mysql->Execute(sql.str(), error_message);
}

bool MySqlAccountRepository::UpdateLastLoginTime(std::int64_t account_id, std::string* error_message) {
    auto mysql_lease = mysql_pool_ != nullptr ? std::optional<common::mysql::MySqlClientPool::Lease>(mysql_pool_->Acquire())
                                              : std::nullopt;
    auto* mysql = mysql_lease.has_value() ? mysql_lease->operator->() : mysql_client_;
    std::ostringstream sql;
    sql << "UPDATE account SET last_login_time = CURRENT_TIMESTAMP(3), updated_at = CURRENT_TIMESTAMP(3) "
           "WHERE account_id = "
        << account_id;
    std::uint64_t affected_rows = 0;
    return mysql->Execute(sql.str(), error_message, &affected_rows) && affected_rows == 1;
}

std::int64_t MySqlAccountRepository::FindDefaultPlayerId(std::int64_t account_id) const {
    if (player_mysql_pool_ == nullptr) {
        return 0;
    }

    auto mysql = player_mysql_pool_->Acquire();
    std::int64_t default_player_id = 0;
    for (int shard_no = 0; shard_no < 16; ++shard_no) {
        std::ostringstream sql;
        sql << "SELECT MIN(player_id) AS player_id FROM player_profile_" << ShardSuffix(shard_no)
            << " WHERE account_id = " << account_id;
        if (const auto row = mysql->QueryOne(sql.str()); row.has_value() && !row->at("player_id").empty()) {
            const auto player_id = std::stoll(row->at("player_id"));
            if (player_id > 0 && (default_player_id == 0 || player_id < default_player_id)) {
                default_player_id = player_id;
            }
        }
    }
    return default_player_id;
}

}  // namespace login_server::auth
