#include "framework/transport/transport_client.h"

#include "framework/protocol/packet_codec.h"

#include <boost/asio.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace framework::transport {

using boost::asio::ip::tcp;

namespace {

template <typename Starter>
bool RunTimedSocketOperation(boost::asio::io_context& io_context,
                             tcp::socket& socket,
                             boost::asio::steady_timer& timer,
                             int timeout_ms,
                             Starter starter,
                             std::string* error_message,
                             std::size_t* transferred = nullptr) {
    bool operation_completed = false;
    bool timer_completed = false;
    bool timed_out = false;
    boost::system::error_code operation_error;
    std::size_t bytes = 0;

    timer.expires_after(std::chrono::milliseconds(timeout_ms));
    timer.async_wait([&](const boost::system::error_code& error) {
        timer_completed = true;
        if (!error && !operation_completed) {
            timed_out = true;
            boost::system::error_code ignored;
            socket.cancel(ignored);
        }
    });

    starter([&](const boost::system::error_code& error, std::size_t size) {
        operation_completed = true;
        operation_error = error;
        bytes = size;
        boost::system::error_code ignored;
        timer.cancel(ignored);
    });

    io_context.restart();
    while (!(operation_completed && timer_completed)) {
        if (io_context.run_one() == 0) {
            break;
        }
    }

    if (!(operation_completed && timer_completed)) {
        if (error_message != nullptr) {
            *error_message = "transport operation did not complete";
        }
        return false;
    }

    if (timed_out) {
        if (error_message != nullptr) {
            *error_message = "timeout";
        }
        return false;
    }

    if (operation_error) {
        if (error_message != nullptr) {
            *error_message = operation_error.message();
        }
        return false;
    }

    if (transferred != nullptr) {
        *transferred = bytes;
    }
    return true;
}

}  // namespace

struct TransportClient::Impl {
    Impl(std::string server_host, int server_port, int server_timeout_ms)
        : host(std::move(server_host)),
          port(server_port),
          timeout_ms(server_timeout_ms),
          resolver(io_context),
          socket(io_context),
          timer(io_context) {}

    std::string host;
    int port = 0;
    int timeout_ms = 0;
    boost::asio::io_context io_context;
    tcp::resolver resolver;
    tcp::socket socket;
    boost::asio::steady_timer timer;
    std::mutex mutex;
};

TransportClient::TransportClient(std::string host, int port, int timeout_ms)
    : impl_(new Impl(std::move(host), port, timeout_ms)) {}

TransportClient::~TransportClient() {
    Close();
    delete impl_;
    impl_ = nullptr;
}

bool TransportClient::SendAndReceive(const common::net::Packet& request,
                                     common::net::Packet* response,
                                     std::string* error_message) {
    std::lock_guard lock(impl_->mutex);

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

void TransportClient::Close() {
    if (impl_ == nullptr) {
        return;
    }

    boost::system::error_code ignored;
    impl_->timer.cancel(ignored);
    impl_->socket.close(ignored);
}

bool TransportClient::EnsureConnected(std::string* error_message) {
    if (impl_->socket.is_open()) {
        return true;
    }
    return Connect(error_message);
}

bool TransportClient::Connect(std::string* error_message) {
    boost::system::error_code resolve_error;
    const auto endpoints = impl_->resolver.resolve(impl_->host, std::to_string(impl_->port), resolve_error);
    if (resolve_error) {
        if (error_message != nullptr) {
            *error_message = resolve_error.message();
        }
        return false;
    }

    boost::system::error_code ignored;
    impl_->socket.close(ignored);
    impl_->socket = tcp::socket(impl_->io_context);

    return RunTimedSocketOperation(
        impl_->io_context,
        impl_->socket,
        impl_->timer,
        impl_->timeout_ms,
        [&](auto handler) {
            boost::asio::async_connect(impl_->socket,
                                       endpoints,
                                       [handler](const boost::system::error_code& error, const tcp::endpoint&) {
                                           handler(error, 0);
                                       });
        },
        error_message);
}

bool TransportClient::WritePacket(const common::net::Packet& request, std::string* error_message) {
    const auto encoded = framework::protocol::EncodePacket(request);
    return RunTimedSocketOperation(
        impl_->io_context,
        impl_->socket,
        impl_->timer,
        impl_->timeout_ms,
        [&](auto handler) {
            boost::asio::async_write(impl_->socket,
                                     boost::asio::buffer(encoded),
                                     [handler](const boost::system::error_code& error, std::size_t bytes) {
                                         handler(error, bytes);
                                     });
        },
        error_message);
}

bool TransportClient::ReadPacket(common::net::Packet* response, std::string* error_message) {
    std::array<char, framework::protocol::kPacketHeaderSize> header_buffer{};
    if (!RunTimedSocketOperation(
            impl_->io_context,
            impl_->socket,
            impl_->timer,
            impl_->timeout_ms,
            [&](auto handler) {
                boost::asio::async_read(impl_->socket,
                                        boost::asio::buffer(header_buffer),
                                        [handler](const boost::system::error_code& error, std::size_t bytes) {
                                            handler(error, bytes);
                                        });
            },
            error_message)) {
        return false;
    }

    common::net::PacketHeader header{};
    if (!framework::protocol::DecodeHeader(
            std::string_view(header_buffer.data(), header_buffer.size()), &header, error_message)) {
        return false;
    }

    std::string body(header.body_len, '\0');
    if (header.body_len > 0 &&
        !RunTimedSocketOperation(
            impl_->io_context,
            impl_->socket,
            impl_->timer,
            impl_->timeout_ms,
            [&](auto handler) {
                boost::asio::async_read(impl_->socket,
                                        boost::asio::buffer(body),
                                        [handler](const boost::system::error_code& error, std::size_t bytes) {
                                            handler(error, bytes);
                                        });
            },
            error_message)) {
        return false;
    }

    if (response != nullptr) {
        response->header = header;
        response->body = std::move(body);
    }
    return true;
}

UpstreamClientPool::UpstreamClientPool(std::string host, int port, int timeout_ms, int pool_size) {
    const auto size = pool_size > 0 ? pool_size : 1;
    clients_.reserve(static_cast<std::size_t>(size));
    for (int index = 0; index < size; ++index) {
        clients_.push_back(std::make_unique<TransportClient>(host, port, timeout_ms));
    }
}

bool UpstreamClientPool::SendAndReceive(const common::net::Packet& request,
                                        common::net::Packet* response,
                                        std::string* error_message) {
    if (clients_.empty()) {
        if (error_message != nullptr) {
            *error_message = "no upstream clients configured";
        }
        return false;
    }

    const auto index = next_index_.fetch_add(1) % clients_.size();
    return clients_[index]->SendAndReceive(request, response, error_message);
}

}  // namespace framework::transport
