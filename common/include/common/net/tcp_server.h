#pragma once

#include "common/net/packet.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace common::net {

struct IncomingPacket {
    std::uint64_t connection_id = 0;
    Packet packet;
    std::string peer_address;
};

using PacketHandler = std::function<std::optional<Packet>(const IncomingPacket&)>;
using DisconnectHandler = std::function<void(std::uint64_t)>;

class EpollTcpServer {
public:
    EpollTcpServer();
    ~EpollTcpServer();

    EpollTcpServer(const EpollTcpServer&) = delete;
    EpollTcpServer& operator=(const EpollTcpServer&) = delete;

    bool Listen(const std::string& host, int port, std::string* error_message);
    void SetPacketHandler(PacketHandler handler);
    void SetDisconnectHandler(DisconnectHandler handler);
    int Run(const std::function<bool()>& keep_running);

private:
    struct Connection {
        int fd = -1;
        std::uint64_t id = 0;
        std::string peer_address;
        std::string read_buffer;
        std::deque<std::string> write_queue;
    };

    void CloseConnection(int fd);
    bool AcceptConnections(std::string* error_message);
    bool HandleRead(Connection& connection);
    bool HandleWrite(Connection& connection);
    void QueueResponse(Connection& connection, const Packet& packet);

    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    std::uint64_t next_connection_id_ = 1;
    PacketHandler packet_handler_;
    DisconnectHandler disconnect_handler_;
    std::unordered_map<int, Connection> connections_;
};

}  // namespace common::net
