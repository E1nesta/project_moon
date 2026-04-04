#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "modules/login/application/account_repository.h"

#include <unordered_map>

namespace login_server::auth {

class InMemoryAccountRepository final : public AccountRepository {
public:
    static InMemoryAccountRepository FromConfig(const common::config::SimpleConfig& config);

    [[nodiscard]] std::optional<common::model::Account> FindByName(const std::string& account_name) const override;
    bool RecordLoginAudit(std::int64_t account_id,
                          bool success,
                          const std::string& trace_id,
                          std::string* error_message = nullptr) override;
    bool UpdateLastLoginTime(std::int64_t account_id, std::string* error_message = nullptr) override;

private:
    explicit InMemoryAccountRepository(common::model::Account account);

    std::unordered_map<std::string, common::model::Account> accounts_;
};

}  // namespace login_server::auth
