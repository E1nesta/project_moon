#include "runtime/transport/transport_server.h"

#include "runtime/foundation/log/logger.h"
#include "runtime/observability/structured_log.h"
#include "runtime/protocol/packet_codec.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace framework::transport {

using boost::asio::ip::tcp;

namespace {

template <typename SocketT>
SocketT& LowestLayer(SocketT& socket) {
    return socket;
}

template <typename NextLayerT>
NextLayerT& LowestLayer(boost::asio::ssl::stream<NextLayerT>& stream) {
    return stream.next_layer();
}

template <typename StreamT>
struct IsSslStream : std::false_type {};

template <typename NextLayerT>
struct IsSslStream<boost::asio::ssl::stream<NextLayerT>> : std::true_type {};

std::string FormatPeerAddress(const tcp::endpoint& endpoint) {
    const auto address = endpoint.address().to_string();
    if (endpoint.address().is_v6()) {
        return "[" + address + "]:" + std::to_string(endpoint.port());
    }
    return address + ':' + std::to_string(endpoint.port());
}

}  // namespace

struct TransportServer::Impl {
    explicit Impl(Options server_options)
        : options(std::move(server_options)),
          work_guard(boost::asio::make_work_guard(io_context)) {}

    struct SessionBase {
        virtual ~SessionBase() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void ForceClose() = 0;
    };

    Options options;
    boost::asio::io_context io_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard;
    std::unique_ptr<tcp::acceptor> acceptor;
    std::unique_ptr<boost::asio::ssl::context> ssl_context;
    PacketHandler packet_handler;
    DisconnectHandler disconnect_handler;
    std::atomic<std::uint64_t> next_connection_id{1};
    std::atomic_bool stopping{false};
    std::mutex sessions_mutex;
    std::condition_variable sessions_cv;
    std::unordered_map<std::uint64_t, std::shared_ptr<SessionBase>> sessions;
    std::vector<std::thread> threads;
};

template <typename StreamT>
class TransportSessionImpl final
    : public TransportServer::Impl::SessionBase,
      public std::enable_shared_from_this<TransportSessionImpl<StreamT>> {
public:
    template <typename StreamFactory>
    TransportSessionImpl(TransportServer::Impl& impl,
                         StreamFactory&& stream_factory,
                         std::uint64_t connection_id,
                         std::string peer_address)
        : impl_(impl),
          stream_(std::forward<StreamFactory>(stream_factory)()),
          strand_(boost::asio::make_strand(impl_.io_context)),
          timer_(LowestLayer(stream_).get_executor()),
          connection_id_(connection_id),
          peer_address_(std::move(peer_address)) {}

    void Start() override {
        auto self = this->shared_from_this();
        boost::asio::dispatch(strand_, [self] {
            self->RefreshDeadline();
            self->StartReadLoop();
        });
    }

    void Stop() override {
        auto self = this->shared_from_this();
        boost::asio::post(strand_, [self] {
            self->BeginShutdown();
        });
    }

    void ForceClose() override {
        auto self = this->shared_from_this();
        boost::asio::post(strand_, [self] {
            self->Close();
        });
    }

private:
    void StartReadLoop() {
        if constexpr (IsSslStream<StreamT>::value) {
            DoHandshake();
        } else {
            ReadHeader();
        }
    }

    void DoHandshake() {
        if (closed_ || shutting_down_ || impl_.stopping.load()) {
            return;
        }

        auto self = this->shared_from_this();
        stream_.async_handshake(
            boost::asio::ssl::stream_base::server,
            boost::asio::bind_executor(
                strand_,
                [self](const boost::system::error_code& error) {
                    if (error) {
                        auto error_code = framework::observability::ClassifyTransportError(error.message());
                        if (error_code == framework::observability::LogErrorCode::kUpstreamRequestFailed) {
                            error_code = framework::observability::LogErrorCode::kTlsHandshakeFailed;
                        }
                        framework::observability::EventLogBuilder(
                            framework::observability::LogEvent::kTransportTlsHandshakeFailed)
                            .AddField("connection_id", self->connection_id_)
                            .AddField("peer.address", self->peer_address_)
                            .WithErrorCode(error_code)
                            .WithDetail("tls handshake failed: " + error.message())
                            .Emit(common::log::LogLevel::kWarn);
                        self->Close();
                        return;
                    }
                    self->RefreshDeadline();
                    self->ReadHeader();
                }));
    }

    void RefreshDeadline() {
        if (impl_.options.idle_timeout_ms <= 0 || closed_) {
            return;
        }

        timer_.expires_after(std::chrono::milliseconds(impl_.options.idle_timeout_ms));
        auto self = this->shared_from_this();
        timer_.async_wait(boost::asio::bind_executor(
            strand_,
            [self](const boost::system::error_code& error) {
                if (!error) {
                    self->Close();
                }
            }));
    }

    void ReadHeader() {
        if (closed_ || shutting_down_ || impl_.stopping.load()) {
            return;
        }

        auto self = this->shared_from_this();
        boost::asio::async_read(stream_,
                                boost::asio::buffer(read_header_),
                                boost::asio::bind_executor(
                                    strand_,
                                    [self](const boost::system::error_code& error, std::size_t /*bytes*/) {
                                        self->OnReadHeader(error);
                                    }));
    }

    void OnReadHeader(const boost::system::error_code& error) {
        if (error) {
            Close();
            return;
        }

        RefreshDeadline();

        common::net::PacketHeader header{};
        std::string error_message;
        if (!framework::protocol::DecodeHeader(
                std::string_view(read_header_.data(), read_header_.size()),
                &header,
                &error_message,
                impl_.options.max_packet_body_bytes)) {
            Close();
            return;
        }

        read_body_.assign(header.body_len, '\0');
        if (header.body_len == 0) {
            DispatchPacket({header, {}});
            return;
        }

        auto self = this->shared_from_this();
        boost::asio::async_read(stream_,
                                boost::asio::buffer(read_body_),
                                boost::asio::bind_executor(
                                    strand_,
                                    [self, header](const boost::system::error_code& body_error, std::size_t /*bytes*/) {
                                        self->OnReadBody(header, body_error);
                                    }));
    }

    void OnReadBody(const common::net::PacketHeader& header, const boost::system::error_code& error) {
        if (error) {
            Close();
            return;
        }

        RefreshDeadline();
        DispatchPacket({header, read_body_});
    }

    void DispatchPacket(common::net::Packet packet) {
        if (closed_ || request_in_flight_) {
            return;
        }

        if (impl_.stopping.load() && !shutting_down_) {
            CloseIfIdle();
            return;
        }

        request_in_flight_ = true;
        if (impl_.packet_handler) {
            auto self = this->shared_from_this();
            impl_.packet_handler(
                {connection_id_, std::move(packet), peer_address_},
                [self](common::net::Packet response) {
                    boost::asio::post(self->strand_, [self, response = std::move(response)]() mutable {
                        self->OnResponseReady(std::move(response));
                    });
                });
            return;
        }

        request_in_flight_ = false;
        CloseIfIdle();
    }

    void QueueWrite(const common::net::Packet& packet) {
        if (closed_) {
            return;
        }

        if (write_queue_.size() >= impl_.options.write_queue_limit) {
            Close();
            return;
        }

        write_queue_.push_back(framework::protocol::EncodePacket(packet));
        if (!write_in_progress_) {
            write_in_progress_ = true;
            WriteNext();
        }
    }

    void WriteNext() {
        if (closed_ || write_queue_.empty()) {
            return;
        }

        auto self = this->shared_from_this();
        boost::asio::async_write(stream_,
                                 boost::asio::buffer(write_queue_.front()),
                                 boost::asio::bind_executor(
                                     strand_,
                                     [self](const boost::system::error_code& error, std::size_t /*bytes*/) {
                                         self->OnWrite(error);
                                     }));
    }

    void OnWrite(const boost::system::error_code& error) {
        if (error) {
            Close();
            return;
        }

        RefreshDeadline();
        write_queue_.pop_front();
        if (!write_queue_.empty()) {
            WriteNext();
            return;
        }

        write_in_progress_ = false;
        request_in_flight_ = false;
        if (shutting_down_) {
            CloseIfIdle();
            return;
        }

        ReadHeader();
    }

    void BeginShutdown() {
        if (closed_) {
            return;
        }

        shutting_down_ = true;
        CloseIfIdle();
    }

    void Close() {
        if (closed_) {
            return;
        }
        closed_ = true;

        boost::system::error_code ignored;
        timer_.cancel(ignored);
        LowestLayer(stream_).shutdown(tcp::socket::shutdown_both, ignored);
        LowestLayer(stream_).close(ignored);
        if (impl_.disconnect_handler) {
            impl_.disconnect_handler(connection_id_);
        }
        {
            std::lock_guard lock(impl_.sessions_mutex);
            impl_.sessions.erase(connection_id_);
        }
        impl_.sessions_cv.notify_all();
    }

    void OnResponseReady(common::net::Packet packet) {
        if (closed_) {
            return;
        }

        QueueWrite(packet);
        if (closed_) {
            return;
        }

        if (!write_in_progress_) {
            request_in_flight_ = false;
            if (shutting_down_) {
                CloseIfIdle();
                return;
            }
            ReadHeader();
        }
    }

    void CloseIfIdle() {
        if (!request_in_flight_ && !write_in_progress_ && write_queue_.empty()) {
            Close();
        }
    }

    TransportServer::Impl& impl_;
    StreamT stream_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::steady_timer timer_;
    std::uint64_t connection_id_ = 0;
    std::string peer_address_;
    std::array<char, framework::protocol::kPacketHeaderSize> read_header_{};
    std::string read_body_;
    std::deque<std::string> write_queue_;
    bool write_in_progress_ = false;
    bool request_in_flight_ = false;
    bool shutting_down_ = false;
    bool closed_ = false;
};

template <typename StreamT>
std::shared_ptr<TransportServer::Impl::SessionBase> MakeTransportSession(TransportServer::Impl& impl,
                                                                         StreamT&& stream_factory,
                                                                         std::uint64_t connection_id,
                                                                         std::string peer_address) {
    using Session = TransportSessionImpl<std::decay_t<std::invoke_result_t<StreamT>>>;
    return std::make_shared<Session>(
        impl, std::forward<StreamT>(stream_factory), connection_id, std::move(peer_address));
}

TransportServer::TransportServer() : TransportServer(Options{}) {}

TransportServer::TransportServer(Options options) : impl_(new Impl(std::move(options))) {}

TransportServer::~TransportServer() {
    Stop();
    delete impl_;
    impl_ = nullptr;
}

bool TransportServer::Start(const std::string& host, int port, std::string* error_message) {
    try {
        tcp::endpoint endpoint;
        if (host.empty() || host == "0.0.0.0") {
            endpoint = tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port));
        } else {
            endpoint = tcp::endpoint(boost::asio::ip::make_address(host), static_cast<unsigned short>(port));
        }

        if (impl_->options.tls.enabled) {
            impl_->ssl_context = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tls_server);
            impl_->ssl_context->use_certificate_chain_file(impl_->options.tls.cert_file);
            impl_->ssl_context->use_private_key_file(
                impl_->options.tls.key_file, boost::asio::ssl::context::pem);
            if (impl_->options.tls.verify_peer) {
                if (!impl_->options.tls.ca_file.empty()) {
                    impl_->ssl_context->load_verify_file(impl_->options.tls.ca_file);
                } else {
                    impl_->ssl_context->set_default_verify_paths();
                }
                impl_->ssl_context->set_verify_mode(boost::asio::ssl::verify_peer |
                                                    boost::asio::ssl::verify_fail_if_no_peer_cert);
            } else {
                impl_->ssl_context->set_verify_mode(boost::asio::ssl::verify_none);
            }
        }

        impl_->acceptor = std::make_unique<tcp::acceptor>(impl_->io_context);
        impl_->acceptor->open(endpoint.protocol());
        impl_->acceptor->set_option(tcp::acceptor::reuse_address(true));
        impl_->acceptor->bind(endpoint);
        impl_->acceptor->listen();
        DoAccept();
        return true;
    } catch (const std::exception& exception) {
        if (error_message != nullptr) {
            *error_message = exception.what();
        }
        return false;
    }
}

void TransportServer::SetPacketHandler(PacketHandler handler) {
    impl_->packet_handler = std::move(handler);
}

void TransportServer::SetDisconnectHandler(DisconnectHandler handler) {
    impl_->disconnect_handler = std::move(handler);
}

int TransportServer::Run(const std::function<bool()>& keep_running, const std::function<void()>& on_stopping) {
    const auto thread_count = impl_->options.io_threads == 0 ? std::size_t{1} : impl_->options.io_threads;
    impl_->threads.reserve(thread_count);
    for (std::size_t index = 0; index < thread_count; ++index) {
        impl_->threads.emplace_back([this] {
            impl_->io_context.run();
        });
    }

    while (keep_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Stop(on_stopping);
    for (auto& thread : impl_->threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    impl_->threads.clear();
    return 0;
}

void TransportServer::Stop(const std::function<void()>& on_stopping) {
    if (impl_ == nullptr || impl_->stopping.exchange(true)) {
        return;
    }

    if (impl_->acceptor) {
        boost::system::error_code ignored;
        impl_->acceptor->cancel(ignored);
        impl_->acceptor->close(ignored);
    }

    if (on_stopping) {
        on_stopping();
    }

    {
        std::lock_guard lock(impl_->sessions_mutex);
        for (auto& [connection_id, session] : impl_->sessions) {
            (void)connection_id;
            if (session != nullptr) {
                session->Stop();
            }
        }
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(0, impl_->options.shutdown_grace_ms));
    while (true) {
        std::unique_lock lock(impl_->sessions_mutex);
        if (impl_->sessions_cv.wait_until(lock, deadline, [this] { return impl_->sessions.empty(); })) {
            break;
        }

        for (auto& [connection_id, session] : impl_->sessions) {
            (void)connection_id;
            if (session != nullptr) {
                session->ForceClose();
            }
        }
        break;
    }

    {
        std::unique_lock lock(impl_->sessions_mutex);
        impl_->sessions_cv.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return impl_->sessions.empty();
        });
    }

    impl_->work_guard.reset();
    impl_->io_context.stop();
}

void TransportServer::DoAccept() {
    if (!impl_->acceptor || impl_->stopping.load()) {
        return;
    }

    impl_->acceptor->async_accept([this](const boost::system::error_code& error, tcp::socket socket) {
        if (!error && !impl_->stopping.load()) {
            boost::system::error_code endpoint_error;
            const auto endpoint = socket.remote_endpoint(endpoint_error);
            std::string peer_address = endpoint_error ? "unknown" : FormatPeerAddress(endpoint);
            const auto connection_id = impl_->next_connection_id.fetch_add(1);

            std::shared_ptr<Impl::SessionBase> session;
            if (impl_->options.tls.enabled && impl_->ssl_context != nullptr) {
                session = MakeTransportSession(
                    *impl_,
                    [socket = std::move(socket), ssl_context = impl_->ssl_context.get()]() mutable {
                        return boost::asio::ssl::stream<decltype(socket)>(std::move(socket), *ssl_context);
                    },
                    connection_id,
                    std::move(peer_address));
            } else {
                session = MakeTransportSession(
                    *impl_,
                    [socket = std::move(socket)]() mutable {
                        return std::move(socket);
                    },
                    connection_id,
                    std::move(peer_address));
            }

            {
                std::lock_guard lock(impl_->sessions_mutex);
                impl_->sessions.emplace(connection_id, session);
            }
            session->Start();
        }

        if (!impl_->stopping.load()) {
            DoAccept();
        }
    });
}

}  // namespace framework::transport
