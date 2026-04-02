#pragma once

#include "common/net/packet.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace framework::transport {

struct TransportInbound {
    std::uint64_t connection_id = 0;
    common::net::Packet packet;
    std::string peer_address;
};

using ResponseCallback = std::function<void(common::net::Packet)>;
using PacketHandler = std::function<void(const TransportInbound&, ResponseCallback)>;
using DisconnectHandler = std::function<void(std::uint64_t)>;

class TransportServer {
public:
    struct Options {
        std::size_t io_threads = 1;
        int idle_timeout_ms = 30000;
        std::size_t write_queue_limit = 128;
        std::uint32_t max_packet_body_bytes = 4U * 1024U * 1024U;
        int shutdown_grace_ms = 5000;
    };

    TransportServer();
    explicit TransportServer(Options options);
    ~TransportServer();

    TransportServer(const TransportServer&) = delete;
    TransportServer& operator=(const TransportServer&) = delete;

    bool Start(const std::string& host, int port, std::string* error_message);
    void SetPacketHandler(PacketHandler handler);
    void SetDisconnectHandler(DisconnectHandler handler);
    int Run(const std::function<bool()>& keep_running, const std::function<void()>& on_stopping = {});
    void Stop(const std::function<void()>& on_stopping = {});

private:
    class TransportSession;

    void DoAccept();

    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace framework::transport
