#include "runtime/transport/transport_client.h"

#include "runtime/protocol/packet_codec.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace framework::transport {

using boost::asio::ip::tcp;
using TlsStream = boost::asio::ssl::stream<tcp::socket>;

namespace {

TransportFailureCode ClassifyOperationFailure(const std::string& error_message, TransportFailureCode fallback) {
    if (error_message == "timeout") {
        return TransportFailureCode::kTimeout;
    }
    if (error_message.find("certificate") != std::string::npos || error_message.find("verify") != std::string::npos ||
        error_message.find("unknown ca") != std::string::npos || error_message.find("bad certificate") != std::string::npos ||
        error_message.find("host name mismatch") != std::string::npos || error_message.find("hostname") != std::string::npos) {
        return TransportFailureCode::kTlsCertificateValidationFailed;
    }
    if (error_message.rfind("tls ", 0) == 0 || error_message.find("ssl") != std::string::npos ||
        error_message.find("handshake") != std::string::npos || error_message.find("alert") != std::string::npos) {
        return TransportFailureCode::kTlsHandshakeFailed;
    }
    return fallback;
}

template <typename CancelFn, typename Starter>
bool RunTimedOperation(boost::asio::io_context& io_context,
                       boost::asio::steady_timer& timer,
                       int timeout_ms,
                       CancelFn&& cancel_operation,
                       Starter&& starter,
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
            cancel_operation();
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

template <typename StreamT>
bool WritePacketWithStream(boost::asio::io_context& io_context,
                           boost::asio::steady_timer& timer,
                           int timeout_ms,
                           StreamT& stream,
                           const common::net::Packet& request,
                           std::string* error_message) {
    const auto encoded = framework::protocol::EncodePacket(request);
    return RunTimedOperation(
        io_context,
        timer,
        timeout_ms,
        [&stream] {
            boost::system::error_code ignored;
            stream.lowest_layer().cancel(ignored);
        },
        [&](auto handler) {
            boost::asio::async_write(stream,
                                     boost::asio::buffer(encoded),
                                     [handler](const boost::system::error_code& error, std::size_t bytes) {
                                         handler(error, bytes);
                                     });
        },
        error_message);
}

bool WritePacketWithSocket(boost::asio::io_context& io_context,
                           boost::asio::steady_timer& timer,
                           int timeout_ms,
                           tcp::socket& socket,
                           const common::net::Packet& request,
                           std::string* error_message) {
    const auto encoded = framework::protocol::EncodePacket(request);
    return RunTimedOperation(
        io_context,
        timer,
        timeout_ms,
        [&socket] {
            boost::system::error_code ignored;
            socket.cancel(ignored);
        },
        [&](auto handler) {
            boost::asio::async_write(socket,
                                     boost::asio::buffer(encoded),
                                     [handler](const boost::system::error_code& error, std::size_t bytes) {
                                         handler(error, bytes);
                                     });
        },
        error_message);
}

template <typename StreamT>
bool ReadPacketWithStream(boost::asio::io_context& io_context,
                          boost::asio::steady_timer& timer,
                          int timeout_ms,
                          StreamT& stream,
                          common::net::Packet* response,
                          std::string* error_message) {
    std::array<char, framework::protocol::kPacketHeaderSize> header_buffer{};
    if (!RunTimedOperation(
            io_context,
            timer,
            timeout_ms,
            [&stream] {
                boost::system::error_code ignored;
                stream.lowest_layer().cancel(ignored);
            },
            [&](auto handler) {
                boost::asio::async_read(stream,
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
        !RunTimedOperation(
            io_context,
            timer,
            timeout_ms,
            [&stream] {
                boost::system::error_code ignored;
                stream.lowest_layer().cancel(ignored);
            },
            [&](auto handler) {
                boost::asio::async_read(stream,
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

bool ReadPacketWithSocket(boost::asio::io_context& io_context,
                          boost::asio::steady_timer& timer,
                          int timeout_ms,
                          tcp::socket& socket,
                          common::net::Packet* response,
                          std::string* error_message) {
    std::array<char, framework::protocol::kPacketHeaderSize> header_buffer{};
    if (!RunTimedOperation(
            io_context,
            timer,
            timeout_ms,
            [&socket] {
                boost::system::error_code ignored;
                socket.cancel(ignored);
            },
            [&](auto handler) {
                boost::asio::async_read(socket,
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
        !RunTimedOperation(
            io_context,
            timer,
            timeout_ms,
            [&socket] {
                boost::system::error_code ignored;
                socket.cancel(ignored);
            },
            [&](auto handler) {
                boost::asio::async_read(socket,
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

}  // namespace

struct TransportClient::Impl {
    Impl(std::string server_host, int server_port, int server_timeout_ms, TlsOptions server_tls_options)
        : host(std::move(server_host)),
          port(server_port),
          timeout_ms(server_timeout_ms),
          tls_options(std::move(server_tls_options)),
          resolver(io_context),
          socket(io_context),
          timer(io_context) {}

    std::string host;
    int port = 0;
    int timeout_ms = 0;
    TlsOptions tls_options;
    boost::asio::io_context io_context;
    tcp::resolver resolver;
    tcp::socket socket;
    boost::asio::steady_timer timer;
    std::unique_ptr<boost::asio::ssl::context> ssl_context;
    std::unique_ptr<TlsStream> tls_stream;
    std::mutex mutex;
};

TransportClient::TransportClient(std::string host, int port, int timeout_ms, TlsOptions tls_options)
    : impl_(new Impl(std::move(host), port, timeout_ms, std::move(tls_options))) {}

TransportClient::~TransportClient() {
    Close();
    delete impl_;
    impl_ = nullptr;
}

bool TransportClient::SendAndReceive(const common::net::Packet& request,
                                     common::net::Packet* response,
                                     std::string* error_message,
                                     TransportFailureCode* failure_code) {
    std::lock_guard lock(impl_->mutex);

    if (failure_code != nullptr) {
        *failure_code = TransportFailureCode::kNone;
    }

    if (!EnsureConnected(error_message, failure_code)) {
        return false;
    }

    if (!WritePacket(request, error_message, failure_code)) {
        Close();
        return false;
    }

    if (!ReadPacket(response, error_message, failure_code)) {
        Close();
        return false;
    }

    // Keep gateway upstream calls request-scoped for MVP stability.
    // This avoids reusing half-closed sockets when upstream services idle out.
    Close();
    return true;
}

void TransportClient::Close() {
    if (impl_ == nullptr) {
        return;
    }

    boost::system::error_code ignored;
    impl_->timer.cancel(ignored);
    if (impl_->tls_stream != nullptr) {
        impl_->tls_stream->lowest_layer().close(ignored);
        impl_->tls_stream.reset();
    }
    impl_->socket.close(ignored);
}

bool TransportClient::EnsureConnected(std::string* error_message, TransportFailureCode* failure_code) {
    if (impl_->tls_options.enabled) {
        return impl_->tls_stream != nullptr && impl_->tls_stream->lowest_layer().is_open() ? true
                                                                                            : Connect(error_message, failure_code);
    }

    if (impl_->socket.is_open()) {
        return true;
    }
    return Connect(error_message, failure_code);
}

bool TransportClient::Connect(std::string* error_message, TransportFailureCode* failure_code) {
    boost::system::error_code resolve_error;
    const auto endpoints = impl_->resolver.resolve(impl_->host, std::to_string(impl_->port), resolve_error);
    if (resolve_error) {
        if (error_message != nullptr) {
            *error_message = resolve_error.message();
        }
        if (failure_code != nullptr) {
            *failure_code = TransportFailureCode::kResolveFailed;
        }
        return false;
    }

    Close();

    if (!impl_->tls_options.enabled) {
        impl_->socket = tcp::socket(impl_->io_context);
        return RunTimedOperation(
            impl_->io_context,
            impl_->timer,
            impl_->timeout_ms,
            [this] {
                boost::system::error_code ignored;
                impl_->socket.cancel(ignored);
            },
            [&](auto handler) {
                boost::asio::async_connect(impl_->socket,
                                           endpoints,
                                           [handler](const boost::system::error_code& error, const tcp::endpoint&) {
                                               handler(error, 0);
                                           });
            },
            error_message)
                   ? true
                   : ([&] {
                         if (failure_code != nullptr) {
                             *failure_code = ClassifyOperationFailure(
                                 error_message != nullptr ? *error_message : std::string{},
                                 TransportFailureCode::kConnectFailed);
                         }
                         return false;
                     })();
    }

    impl_->ssl_context = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tls_client);
    if (impl_->tls_options.verify_peer) {
        if (!impl_->tls_options.ca_file.empty()) {
            impl_->ssl_context->load_verify_file(impl_->tls_options.ca_file);
        } else {
            impl_->ssl_context->set_default_verify_paths();
        }
        impl_->ssl_context->set_verify_mode(boost::asio::ssl::verify_peer);
    } else {
        impl_->ssl_context->set_verify_mode(boost::asio::ssl::verify_none);
    }

    impl_->tls_stream = std::make_unique<TlsStream>(impl_->io_context, *impl_->ssl_context);
    const auto server_name = impl_->tls_options.server_name.empty() ? impl_->host : impl_->tls_options.server_name;
    if (impl_->tls_options.verify_peer && !server_name.empty()) {
        impl_->tls_stream->set_verify_callback(boost::asio::ssl::rfc2818_verification(server_name));
    }
    if (!server_name.empty() &&
        SSL_set_tlsext_host_name(impl_->tls_stream->native_handle(), server_name.c_str()) != 1) {
        if (error_message != nullptr) {
            *error_message = "tls setup failed: failed to set tls server name";
        }
        if (failure_code != nullptr) {
            *failure_code = TransportFailureCode::kTlsSetupFailed;
        }
        return false;
    }

    if (!RunTimedOperation(
            impl_->io_context,
            impl_->timer,
            impl_->timeout_ms,
            [this] {
                boost::system::error_code ignored;
                if (impl_->tls_stream != nullptr) {
                    impl_->tls_stream->lowest_layer().cancel(ignored);
                }
            },
            [&](auto handler) {
                boost::asio::async_connect(
                    impl_->tls_stream->lowest_layer(),
                    endpoints,
                    [handler](const boost::system::error_code& error, const tcp::endpoint&) {
                        handler(error, 0);
                    });
            },
            error_message)) {
        if (failure_code != nullptr) {
            *failure_code = ClassifyOperationFailure(
                error_message != nullptr ? *error_message : std::string{},
                TransportFailureCode::kConnectFailed);
        }
        return false;
    }

    const auto handshake_ok = RunTimedOperation(
        impl_->io_context,
        impl_->timer,
        impl_->timeout_ms,
        [this] {
            boost::system::error_code ignored;
            if (impl_->tls_stream != nullptr) {
                impl_->tls_stream->lowest_layer().cancel(ignored);
            }
        },
        [&](auto handler) {
            impl_->tls_stream->async_handshake(
                boost::asio::ssl::stream_base::client,
                [handler](const boost::system::error_code& error) {
                    handler(error, 0);
                });
        },
        error_message);
    if (!handshake_ok && error_message != nullptr && *error_message != "timeout" &&
        error_message->rfind("tls ", 0) != 0) {
        *error_message = "tls handshake failed: " + *error_message;
    }
    if (!handshake_ok && failure_code != nullptr) {
        *failure_code = ClassifyOperationFailure(
            error_message != nullptr ? *error_message : std::string{},
            TransportFailureCode::kTlsHandshakeFailed);
    }
    return handshake_ok;
}

bool TransportClient::WritePacket(const common::net::Packet& request,
                                  std::string* error_message,
                                  TransportFailureCode* failure_code) {
    const auto ok = impl_->tls_options.enabled && impl_->tls_stream != nullptr
                        ? WritePacketWithStream(
                              impl_->io_context, impl_->timer, impl_->timeout_ms, *impl_->tls_stream, request, error_message)
                        : WritePacketWithSocket(
                              impl_->io_context, impl_->timer, impl_->timeout_ms, impl_->socket, request, error_message);
    if (!ok && failure_code != nullptr) {
        *failure_code = ClassifyOperationFailure(
            error_message != nullptr ? *error_message : std::string{},
            TransportFailureCode::kWriteFailed);
    }
    return ok;
}

bool TransportClient::ReadPacket(common::net::Packet* response,
                                 std::string* error_message,
                                 TransportFailureCode* failure_code) {
    const auto ok = impl_->tls_options.enabled && impl_->tls_stream != nullptr
                        ? ReadPacketWithStream(
                              impl_->io_context, impl_->timer, impl_->timeout_ms, *impl_->tls_stream, response, error_message)
                        : ReadPacketWithSocket(
                              impl_->io_context, impl_->timer, impl_->timeout_ms, impl_->socket, response, error_message);
    if (!ok && failure_code != nullptr) {
        *failure_code = ClassifyOperationFailure(
            error_message != nullptr ? *error_message : std::string{},
            TransportFailureCode::kReadFailed);
    }
    return ok;
}

UpstreamClientPool::UpstreamClientPool(std::string host, int port, int timeout_ms, int pool_size, TlsOptions tls_options) {
    const auto size = pool_size > 0 ? pool_size : 1;
    clients_.reserve(static_cast<std::size_t>(size));
    for (int index = 0; index < size; ++index) {
        clients_.push_back(std::make_unique<TransportClient>(host, port, timeout_ms, tls_options));
    }
}

bool UpstreamClientPool::SendAndReceive(const common::net::Packet& request,
                                        common::net::Packet* response,
                                        std::string* error_message,
                                        TransportFailureCode* failure_code) {
    if (clients_.empty()) {
        if (error_message != nullptr) {
            *error_message = "no upstream clients configured";
        }
        if (failure_code != nullptr) {
            *failure_code = TransportFailureCode::kNoUpstreamClients;
        }
        return false;
    }

    const auto index = next_index_.fetch_add(1) % clients_.size();
    return clients_[index]->SendAndReceive(request, response, error_message, failure_code);
}

}  // namespace framework::transport
