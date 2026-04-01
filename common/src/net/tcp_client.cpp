#include "common/net/tcp_client.h"

#include "common/net/packet.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace common::net {

PersistentTcpClient::PersistentTcpClient(std::string host, int port, int timeout_ms)
    : host_(std::move(host)), port_(port), timeout_ms_(timeout_ms) {}

PersistentTcpClient::~PersistentTcpClient() {
    Close();
}

bool PersistentTcpClient::SendAndReceive(const Packet& request, Packet* response, std::string* error_message) {
    std::lock_guard lock(mutex_);

    if (!EnsureConnected(error_message)) {
        return false;
    }

    if (!WritePacket(request, error_message)) {
        Close();
        return false;
    }

    if (!ReadPacket(response, error_message)) {
        Close();
        return false;
    }

    return true;
}

void PersistentTcpClient::Close() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool PersistentTcpClient::EnsureConnected(std::string* error_message) {
    if (socket_fd_ >= 0) {
        return true;
    }

    return Connect(error_message);
}

bool PersistentTcpClient::Connect(std::string* error_message) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const auto port_text = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), port_text.c_str(), &hints, &results) != 0) {
        if (error_message != nullptr) {
            *error_message = "getaddrinfo failed for " + host_ + ':' + port_text;
        }
        return false;
    }

    for (auto* result = results; result != nullptr; result = result->ai_next) {
        const int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect(fd, result->ai_addr, result->ai_addrlen) == 0) {
            socket_fd_ = fd;
            freeaddrinfo(results);
            return true;
        }

        close(fd);
    }

    freeaddrinfo(results);
    if (error_message != nullptr) {
        *error_message = "connect failed to " + host_ + ':' + port_text;
    }
    return false;
}

bool PersistentTcpClient::WritePacket(const Packet& request, std::string* error_message) {
    std::string encoded = EncodePacket(request.header, request.body);
    std::size_t written_total = 0;
    while (written_total < encoded.size()) {
        if (!WaitForEvent(POLLOUT, error_message)) {
            return false;
        }

        const auto written = send(socket_fd_,
                                  encoded.data() + static_cast<std::ptrdiff_t>(written_total),
                                  encoded.size() - written_total,
                                  0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error_message != nullptr) {
                *error_message = std::strerror(errno);
            }
            return false;
        }
        written_total += static_cast<std::size_t>(written);
    }
    return true;
}

bool PersistentTcpClient::ReadPacket(Packet* response, std::string* error_message) {
    std::string buffer;
    buffer.reserve(4096);

    while (true) {
        Packet packet;
        if (TryExtractPacket(buffer, &packet)) {
            if (response != nullptr) {
                *response = std::move(packet);
            }
            return true;
        }

        if (!WaitForEvent(POLLIN, error_message)) {
            return false;
        }

        char temp[4096];
        const auto bytes = recv(socket_fd_, temp, sizeof(temp), 0);
        if (bytes == 0) {
            if (error_message != nullptr) {
                *error_message = "peer closed connection";
            }
            return false;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error_message != nullptr) {
                *error_message = std::strerror(errno);
            }
            return false;
        }
        buffer.append(temp, static_cast<std::size_t>(bytes));
    }
}

bool PersistentTcpClient::WaitForEvent(short events, std::string* error_message) {
    pollfd descriptor{};
    descriptor.fd = socket_fd_;
    descriptor.events = events;
    const auto ready = poll(&descriptor, 1, timeout_ms_);
    if (ready == 0) {
        if (error_message != nullptr) {
            *error_message = "timeout";
        }
        return false;
    }
    if (ready < 0) {
        if (error_message != nullptr) {
            *error_message = std::strerror(errno);
        }
        return false;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        if (error_message != nullptr) {
            *error_message = "socket error";
        }
        return false;
    }
    return true;
}

UpstreamClientPool::UpstreamClientPool(std::string host, int port, int timeout_ms, int pool_size) {
    const auto size = pool_size > 0 ? pool_size : 1;
    clients_.reserve(static_cast<std::size_t>(size));
    for (int index = 0; index < size; ++index) {
        clients_.push_back(std::make_unique<PersistentTcpClient>(host, port, timeout_ms));
    }
}

bool UpstreamClientPool::SendAndReceive(const Packet& request, Packet* response, std::string* error_message) {
    if (clients_.empty()) {
        if (error_message != nullptr) {
            *error_message = "no upstream clients configured";
        }
        return false;
    }

    const auto index = next_index_.fetch_add(1) % clients_.size();
    return clients_[index]->SendAndReceive(request, response, error_message);
}

}  // namespace common::net
