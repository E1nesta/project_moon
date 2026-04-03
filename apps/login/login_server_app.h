#pragma once

#include "runtime/storage/mysql/mysql_client_pool.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "runtime/session/session_store.h"
#include "runtime/transport/service_app.h"
#include "modules/login/infrastructure/mysql_account_repository.h"
#include "modules/login/application/login_service.h"

#include <memory>

namespace services::login {

// Thin adapter for the login service process.
class LoginServerApp : public framework::service::ServiceApp {
public:
    LoginServerApp();

protected:
    bool BuildDependencies(std::string* error_message) override;
    void RegisterRoutes() override;
    bool RequiresTrustedGateway() const override { return true; }

private:
    // Adapter entrypoint: parse request, invoke the login application service and map the response packet.
    common::net::Packet HandleLoginRequest(const framework::protocol::HandlerContext& context,
                                           const common::net::Packet& packet) const;

    std::unique_ptr<common::mysql::MySqlClientPool> mysql_pool_;
    std::unique_ptr<common::redis::RedisClientPool> redis_pool_;
    std::unique_ptr<login_server::auth::MySqlAccountRepository> account_repository_;
    std::unique_ptr<common::session::SessionStore> session_repository_;
    std::unique_ptr<login_server::LoginService> login_service_;
};

}  // namespace services::login
