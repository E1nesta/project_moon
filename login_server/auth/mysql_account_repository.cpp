#include "login_server/auth/mysql_account_repository.h"

#include <sstream>

namespace login_server::auth {

MySqlAccountRepository::MySqlAccountRepository(common::mysql::MySqlClient& mysql_client) : mysql_client_(mysql_client) {}

std::optional<common::model::Account> MySqlAccountRepository::FindByName(const std::string& account_name) const {
    std::ostringstream sql;
    sql << "SELECT a.account_id, a.account_name, a.password_hash, a.status, "
           "COALESCE(MIN(p.player_id), 0) AS default_player_id "
           "FROM account a "
           "LEFT JOIN player p ON p.account_id = a.account_id "
           "WHERE a.account_name = '"
        << mysql_client_.Escape(account_name)
        << "' GROUP BY a.account_id, a.account_name, a.password_hash, a.status LIMIT 1";

    const auto row = mysql_client_.QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    common::model::Account account;
    account.account_id = std::stoll(row->at("account_id"));
    account.account_name = row->at("account_name");
    account.password = row->at("password_hash");
    account.default_player_id = std::stoll(row->at("default_player_id"));
    account.enabled = row->at("status") == "1";
    return account;
}

}  // namespace login_server::auth
