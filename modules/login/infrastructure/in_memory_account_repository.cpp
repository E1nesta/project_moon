#include "modules/login/infrastructure/in_memory_account_repository.h"

#include "runtime/foundation/security/password_hasher.h"

namespace login_server::auth {

InMemoryAccountRepository InMemoryAccountRepository::FromConfig(const common::config::SimpleConfig& config) {
    common::model::Account account;
    account.account_id = config.GetInt("demo.account_id", 10001);
    account.account_name = config.GetString("demo.account_name", "demo");
    account.password_hash = config.GetString("demo.password_hash");
    if (account.password_hash.empty()) {
        const auto encoded_hash = common::security::PasswordHasher::BuildEncodedHash(
            config.GetString("demo.password", "demo123"),
            "demo-local-salt");
        account.password_hash = encoded_hash.value_or("");
    }
    account.default_player_id = config.GetInt("demo.default_player_id", 20001);
    account.enabled = true;
    account.realname_verified = true;
    return InMemoryAccountRepository(account);
}

std::optional<common::model::Account> InMemoryAccountRepository::FindByName(const std::string& account_name) const {
    const auto iter = accounts_.find(account_name);
    if (iter == accounts_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

bool InMemoryAccountRepository::RecordLoginAudit(std::int64_t /*account_id*/,
                                                 bool /*success*/,
                                                 const std::string& /*trace_id*/,
                                                 std::string* /*error_message*/) {
    return true;
}

bool InMemoryAccountRepository::UpdateLastLoginTime(std::int64_t account_id, std::string* error_message) {
    for (auto& [name, account] : accounts_) {
        if (account.account_id == account_id) {
            return true;
        }
    }
    if (error_message != nullptr) {
        *error_message = "account not found";
    }
    return false;
}

InMemoryAccountRepository::InMemoryAccountRepository(common::model::Account account) {
    accounts_.emplace(account.account_name, std::move(account));
}

}  // namespace login_server::auth
