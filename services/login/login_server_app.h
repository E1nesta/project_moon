#pragma once

#include "common/mysql/mysql_client_pool.h"
#include "common/redis/redis_client_pool.h"
#include "framework/service/service_app.h"
#include "login_server/auth/mysql_account_repository.h"
#include "login_server/login_service.h"
#include "login_server/session/redis_session_repository.h"

#include <memory>

namespace services::login {

// Thin adapter for the login service process.
class LoginServerApp : public framework::service::ServiceApp {
public:
    LoginServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    void RegisterRoutes() override;

private:
    // Adapter entrypoint: parse request, invoke the login application service and map the response packet.
    common::net::Packet HandleLoginRequest(const framework::protocol::HandlerContext& context,
                                           const common::net::Packet& packet) const;

    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<login_server::auth::MySqlAccountRepository> account_repository_;
    std::unique_ptr<login_server::session::RedisSessionRepository> session_repository_;
    std::unique_ptr<login_server::LoginService> login_service_;
};

}  // namespace services::login
