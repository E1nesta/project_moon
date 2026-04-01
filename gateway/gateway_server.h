#pragma once

#include "common/config/simple_config.h"
#include "common/error/error_code.h"
#include "common/net/message_id.h"
#include "common/net/request_context.h"
#include "common/net/tcp_client.h"
#include "common/net/tcp_server.h"

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
    struct ClientBinding {
        std::string session_id;
        std::int64_t player_id = 0;
    };

    std::optional<common::net::Packet> HandlePacket(const common::net::IncomingPacket& incoming);
    auto ResolveUpstream(common::net::MessageId message_id) -> common::net::UpstreamClientPool&;
    common::error::ErrorCode MapUpstreamError(const std::string& error_message) const;

    common::config::SimpleConfig config_;
    common::net::EpollTcpServer server_;
    std::unique_ptr<common::net::UpstreamClientPool> login_upstream_;
    std::unique_ptr<common::net::UpstreamClientPool> game_upstream_;
    std::unique_ptr<common::net::UpstreamClientPool> dungeon_upstream_;
    std::unordered_map<std::uint64_t, ClientBinding> client_bindings_;
};

}  // namespace gateway
