#include "login_server/auth/in_memory_account_repository.h"

namespace login_server::auth {

InMemoryAccountRepository InMemoryAccountRepository::FromConfig(const common::config::SimpleConfig& config) {
    common::model::Account account;
    account.account_id = config.GetInt("demo.account_id", 10001);
    account.account_name = config.GetString("demo.account_name", "demo");
    account.password = config.GetString("demo.password", "demo123");
    account.default_player_id = config.GetInt("demo.default_player_id", 20001);
    account.enabled = true;
    return InMemoryAccountRepository(account);
}

std::optional<common::model::Account> InMemoryAccountRepository::FindByName(const std::string& account_name) const {
    const auto iter = accounts_.find(account_name);
    if (iter == accounts_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

InMemoryAccountRepository::InMemoryAccountRepository(common::model::Account account) {
    accounts_.emplace(account.account_name, std::move(account));
}

}  // namespace login_server::auth

