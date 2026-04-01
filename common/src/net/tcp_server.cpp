#include "common/net/tcp_server.h"

#include "common/net/packet.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
namespace common::net {

namespace {

bool SetNonBlocking(int fd) {
    const auto flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

EpollTcpServer::EpollTcpServer() = default;

EpollTcpServer::~EpollTcpServer() {
    for (auto& [fd, connection] : connections_) {
        (void)connection;
        close(fd);
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

bool EpollTcpServer::Listen(const std::string& host, int port, std::string* error_message) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        if (error_message != nullptr) {
            *error_message = std::strerror(errno);
        }
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (!SetNonBlocking(listen_fd_)) {
        if (error_message != nullptr) {
            *error_message = "failed to set listen socket nonblocking";
        }
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (host.empty() || host == "0.0.0.0") {
        address.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        if (error_message != nullptr) {
            *error_message = "invalid host: " + host;
        }
        return false;
    }

    if (bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (error_message != nullptr) {
            *error_message = std::strerror(errno);
        }
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) != 0) {
        if (error_message != nullptr) {
            *error_message = std::strerror(errno);
        }
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        if (error_message != nullptr) {
            *error_message = std::strerror(errno);
        }
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) != 0) {
        if (error_message != nullptr) {
            *error_message = std::strerror(errno);
        }
        return false;
    }

    return true;
}

void EpollTcpServer::SetPacketHandler(PacketHandler handler) {
    packet_handler_ = std::move(handler);
}

void EpollTcpServer::SetDisconnectHandler(DisconnectHandler handler) {
    disconnect_handler_ = std::move(handler);
}

int EpollTcpServer::Run(const std::function<bool()>& keep_running) {
    constexpr int kMaxEvents = 32;
    epoll_event events[kMaxEvents];

    while (keep_running()) {
        const auto ready = epoll_wait(epoll_fd_, events, kMaxEvents, 200);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 1;
        }

        for (int index = 0; index < ready; ++index) {
            const auto event = events[index];
            if (event.data.fd == listen_fd_) {
                std::string error_message;
                if (!AcceptConnections(&error_message)) {
                    return 1;
                }
                continue;
            }

            auto connection_iter = connections_.find(event.data.fd);
            if (connection_iter == connections_.end()) {
                continue;
            }

            auto& connection = connection_iter->second;
            bool keep_connection = true;
            if ((event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                keep_connection = false;
            }
            if (keep_connection && (event.events & EPOLLIN) != 0) {
                keep_connection = HandleRead(connection);
            }
            if (keep_connection && (event.events & EPOLLOUT) != 0) {
                keep_connection = HandleWrite(connection);
            }

            if (!keep_connection) {
                CloseConnection(connection.fd);
            }
        }
    }

    return 0;
}

bool EpollTcpServer::AcceptConnections(std::string* error_message) {
    while (true) {
        sockaddr_in peer{};
        socklen_t peer_length = sizeof(peer);
        const int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            if (error_message != nullptr) {
                *error_message = std::strerror(errno);
            }
            return false;
        }

        if (!SetNonBlocking(fd)) {
            close(fd);
            continue;
        }

        char address_text[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &peer.sin_addr, address_text, sizeof(address_text));

        Connection connection;
        connection.fd = fd;
        connection.id = next_connection_id_++;
        connection.peer_address = std::string(address_text) + ':' + std::to_string(ntohs(peer.sin_port));

        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) != 0) {
            close(fd);
            continue;
        }

        connections_.emplace(fd, std::move(connection));
    }
}

bool EpollTcpServer::HandleRead(Connection& connection) {
    char buffer[4096];
    while (true) {
        const auto bytes = recv(connection.fd, buffer, sizeof(buffer), 0);
        if (bytes == 0) {
            return false;
        }
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        connection.read_buffer.append(buffer, static_cast<std::size_t>(bytes));
    }

    Packet packet;
    while (TryExtractPacket(connection.read_buffer, &packet)) {
        if (packet_handler_) {
            auto response = packet_handler_({connection.id, packet, connection.peer_address});
            if (response.has_value()) {
                QueueResponse(connection, *response);
            }
        }
    }

    return true;
}

bool EpollTcpServer::HandleWrite(Connection& connection) {
    while (!connection.write_queue.empty()) {
        auto& current = connection.write_queue.front();
        const auto bytes = send(connection.fd, current.data(), current.size(), 0);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (static_cast<std::size_t>(bytes) == current.size()) {
            connection.write_queue.pop_front();
        } else {
            current.erase(0, static_cast<std::size_t>(bytes));
            return true;
        }
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = connection.fd;
    return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection.fd, &event) == 0;
}

void EpollTcpServer::QueueResponse(Connection& connection, const Packet& packet) {
    connection.write_queue.push_back(EncodePacket(packet.header, packet.body));

    epoll_event event{};
    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
    event.data.fd = connection.fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection.fd, &event);
}

void EpollTcpServer::CloseConnection(int fd) {
    auto connection_iter = connections_.find(fd);
    if (connection_iter == connections_.end()) {
        return;
    }

    if (disconnect_handler_) {
        disconnect_handler_(connection_iter->second.id);
    }

    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections_.erase(connection_iter);
}

}  // namespace common::net
