#pragma once

#include "common/mysql/mysql_client.h"
#include "login_server/auth/account_repository.h"

namespace login_server::auth {

class MySqlAccountRepository final : public AccountRepository {
public:
    explicit MySqlAccountRepository(common::mysql::MySqlClient& mysql_client);

    [[nodiscard]] std::optional<common::model::Account> FindByName(const std::string& account_name) const override;

private:
    common::mysql::MySqlClient& mysql_client_;
};

}  // namespace login_server::auth
