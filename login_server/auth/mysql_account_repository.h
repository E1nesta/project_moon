#pragma once

#include "common/mysql/mysql_client_pool.h"
#include "login_server/auth/account_repository.h"

namespace login_server::auth {

// MySQL-backed implementation of the account storage boundary.
class MySqlAccountRepository final : public AccountRepository {
public:
    explicit MySqlAccountRepository(common::mysql::MySqlClientPool& mysql_pool);
    explicit MySqlAccountRepository(common::mysql::MySqlClient& mysql_client);

    [[nodiscard]] std::optional<common::model::Account> FindByName(const std::string& account_name) const override;

private:
    common::mysql::MySqlClientPool* mysql_pool_ = nullptr;
    common::mysql::MySqlClient* mysql_client_ = nullptr;
};

}  // namespace login_server::auth
