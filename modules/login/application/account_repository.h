#pragma once

#include "modules/login/domain/account.h"

#include <cstdint>
#include <optional>
#include <string>

namespace login_server::auth {

// Storage boundary for account lookup used by the login application service.
class AccountRepository {
public:
    virtual ~AccountRepository() = default;

    [[nodiscard]] virtual std::optional<common::model::Account> FindByName(const std::string& account_name) const = 0;
    virtual bool RecordLoginAudit(std::int64_t account_id,
                                  bool success,
                                  const std::string& trace_id,
                                  std::string* error_message = nullptr) = 0;
    virtual bool UpdateLastLoginTime(std::int64_t account_id, std::string* error_message = nullptr) = 0;
};

}  // namespace login_server::auth
