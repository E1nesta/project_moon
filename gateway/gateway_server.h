#pragma once

#include "common/config/simple_config.h"
#include "common/error/error_code.h"
#include "common/net/message_id.h"
#include "common/net/request_context.h"
#include "common/net/tcp_client.h"
#include "common/net/tcp_server.h"
#include "common/redis/redis_client.h"
#include "gateway/session_binding_authorizer.h"
#include "login_server/session/redis_session_repository.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace gateway {

class GatewayServer {
public:
    explicit GatewayServer(common::config::SimpleConfig config);

    bool Initialize(std::string* error_message);
    int Run(const std::function<bool()>& keep_running);

private:
    std::optional<common::net::Packet> HandlePacket(const common::net::IncomingPacket& incoming);
    auto ResolveUpstream(common::net::MessageId message_id) -> common::net::UpstreamClientPool&;
    common::error::ErrorCode MapUpstreamError(const std::string& error_message) const;

    common::config::SimpleConfig config_;
    common::net::EpollTcpServer server_;
    common::redis::RedisClient redis_client_;
    std::unique_ptr<login_server::session::RedisSessionRepository> session_repository_;
    std::unique_ptr<SessionBindingAuthorizer> session_binding_authorizer_;
    std::unique_ptr<common::net::UpstreamClientPool> login_upstream_;
    std::unique_ptr<common::net::UpstreamClientPool> game_upstream_;
    std::unique_ptr<common::net::UpstreamClientPool> dungeon_upstream_;
    std::string instance_id_ = "gateway";
};

}  // namespace gateway
