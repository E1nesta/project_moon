#pragma once

#include "common/config/simple_config.h"
#include "login_server/auth/account_repository.h"

#include <unordered_map>

namespace login_server::auth {

class InMemoryAccountRepository final : public AccountRepository {
public:
    static InMemoryAccountRepository FromConfig(const common::config::SimpleConfig& config);

    [[nodiscard]] std::optional<common::model::Account> FindByName(const std::string& account_name) const override;

private:
    explicit InMemoryAccountRepository(common::model::Account account);

    std::unordered_map<std::string, common::model::Account> accounts_;
};

}  // namespace login_server::auth

