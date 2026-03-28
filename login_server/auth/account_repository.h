#pragma once

#include "common/model/account.h"

#include <optional>
#include <string>

namespace login_server::auth {

class AccountRepository {
public:
    virtual ~AccountRepository() = default;

    [[nodiscard]] virtual std::optional<common::model::Account> FindByName(const std::string& account_name) const = 0;
};

}  // namespace login_server::auth

