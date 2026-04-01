#pragma once

#include "common/net/packet.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace common::net {

class PersistentTcpClient {
public:
    PersistentTcpClient(std::string host, int port, int timeout_ms);
    ~PersistentTcpClient();

    PersistentTcpClient(const PersistentTcpClient&) = delete;
    PersistentTcpClient& operator=(const PersistentTcpClient&) = delete;

    bool SendAndReceive(const Packet& request, Packet* response, std::string* error_message);
    void Close();

private:
    bool EnsureConnected(std::string* error_message);
    bool Connect(std::string* error_message);
    bool WritePacket(const Packet& request, std::string* error_message);
    bool ReadPacket(Packet* response, std::string* error_message);
    bool WaitForEvent(short events, std::string* error_message);

    std::string host_;
    int port_ = 0;
    int timeout_ms_ = 0;
    int socket_fd_ = -1;
    std::mutex mutex_;
};

class UpstreamClientPool {
public:
    UpstreamClientPool(std::string host, int port, int timeout_ms, int pool_size);

    bool SendAndReceive(const Packet& request, Packet* response, std::string* error_message);

private:
    std::vector<std::unique_ptr<PersistentTcpClient>> clients_;
    std::atomic<std::size_t> next_index_{0};
};

}  // namespace common::net
