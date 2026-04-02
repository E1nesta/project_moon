#pragma once

#include "common/net/packet.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace framework::transport {

class TransportClient {
public:
    TransportClient(std::string host, int port, int timeout_ms);
    ~TransportClient();

    TransportClient(const TransportClient&) = delete;
    TransportClient& operator=(const TransportClient&) = delete;

    bool SendAndReceive(const common::net::Packet& request,
                        common::net::Packet* response,
                        std::string* error_message);
    void Close();

private:
    bool EnsureConnected(std::string* error_message);
    bool Connect(std::string* error_message);
    bool WritePacket(const common::net::Packet& request, std::string* error_message);
    bool ReadPacket(common::net::Packet* response, std::string* error_message);

    struct Impl;
    Impl* impl_ = nullptr;
};

class UpstreamClientPool {
public:
    UpstreamClientPool(std::string host, int port, int timeout_ms, int pool_size);

    bool SendAndReceive(const common::net::Packet& request,
                        common::net::Packet* response,
                        std::string* error_message);

private:
    std::vector<std::unique_ptr<TransportClient>> clients_;
    std::atomic<std::size_t> next_index_{0};
};

}  // namespace framework::transport
