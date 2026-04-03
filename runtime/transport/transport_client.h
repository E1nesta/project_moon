#pragma once

#include "runtime/protocol/packet.h"
#include "runtime/transport/tls_options.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace framework::transport {

enum class TransportFailureCode {
    kNone,
    kResolveFailed,
    kConnectFailed,
    kTimeout,
    kTlsSetupFailed,
    kTlsHandshakeFailed,
    kTlsCertificateValidationFailed,
    kWriteFailed,
    kReadFailed,
    kProtocolDecodeFailed,
    kNoUpstreamClients,
};

class TransportClient {
public:
    TransportClient(std::string host, int port, int timeout_ms, TlsOptions tls_options = {});
    ~TransportClient();

    TransportClient(const TransportClient&) = delete;
    TransportClient& operator=(const TransportClient&) = delete;

    bool SendAndReceive(const common::net::Packet& request,
                        common::net::Packet* response,
                        std::string* error_message,
                        TransportFailureCode* failure_code = nullptr);
    void Close();

private:
    bool EnsureConnected(std::string* error_message, TransportFailureCode* failure_code);
    bool Connect(std::string* error_message, TransportFailureCode* failure_code);
    bool WritePacket(const common::net::Packet& request,
                     std::string* error_message,
                     TransportFailureCode* failure_code);
    bool ReadPacket(common::net::Packet* response, std::string* error_message, TransportFailureCode* failure_code);

    struct Impl;
    Impl* impl_ = nullptr;
};

class UpstreamClientPool {
public:
    UpstreamClientPool(std::string host, int port, int timeout_ms, int pool_size, TlsOptions tls_options = {});

    bool SendAndReceive(const common::net::Packet& request,
                        common::net::Packet* response,
                        std::string* error_message,
                        TransportFailureCode* failure_code = nullptr);

private:
    std::vector<std::unique_ptr<TransportClient>> clients_;
    std::atomic<std::size_t> next_index_{0};
};

}  // namespace framework::transport
