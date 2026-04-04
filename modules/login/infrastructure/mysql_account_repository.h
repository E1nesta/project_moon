#pragma once

#include "runtime/storage/mysql/mysql_client_pool.h"
#include "modules/login/application/account_repository.h"

namespace login_server::auth {

// MySQL-backed implementation of the account storage boundary.
class MySqlAccountRepository final : public AccountRepository {
public:
    MySqlAccountRepository(common::mysql::MySqlClientPool& account_mysql_pool,
                           common::mysql::MySqlClientPool& player_mysql_pool);
    explicit MySqlAccountRepository(common::mysql::MySqlClient& mysql_client);

    [[nodiscard]] std::optional<common::model::Account> FindByName(const std::string& account_name) const override;
    bool RecordLoginAudit(std::int64_t account_id,
                          bool success,
                          const std::string& trace_id,
                          std::string* error_message = nullptr) override;
    bool UpdateLastLoginTime(std::int64_t account_id, std::string* error_message = nullptr) override;

private:
    [[nodiscard]] std::int64_t FindDefaultPlayerId(std::int64_t account_id) const;

    common::mysql::MySqlClientPool* mysql_pool_ = nullptr;
    common::mysql::MySqlClientPool* player_mysql_pool_ = nullptr;
    common::mysql::MySqlClient* mysql_client_ = nullptr;
};

}  // namespace login_server::auth
