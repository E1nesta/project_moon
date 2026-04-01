#pragma once

#include "common/config/simple_config.h"
#include "common/mysql/mysql_client.h"
#include "common/net/tcp_server.h"
#include "common/redis/redis_client.h"
#include "login_server/auth/mysql_account_repository.h"
#include "login_server/login_service.h"
#include "login_server/session/redis_session_repository.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace login_server {

class LoginNetworkServer {
public:
    explicit LoginNetworkServer(common::config::SimpleConfig config);

    bool Initialize(std::string* error_message);
    int Run(const std::function<bool()>& keep_running);

private:
    std::optional<common::net::Packet> HandlePacket(const common::net::IncomingPacket& incoming);

    common::config::SimpleConfig config_;
    common::net::EpollTcpServer server_;
    common::mysql::MySqlClient mysql_client_;
    common::redis::RedisClient redis_client_;
    std::unique_ptr<auth::MySqlAccountRepository> account_repository_;
    std::unique_ptr<session::RedisSessionRepository> session_repository_;
    std::unique_ptr<LoginService> login_service_;
};

}  // namespace login_server
