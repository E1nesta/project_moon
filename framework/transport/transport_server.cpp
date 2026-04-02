#include "framework/transport/transport_server.h"

#include "framework/protocol/packet_codec.h"

#include <boost/asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace framework::transport {

using boost::asio::ip::tcp;

struct TransportServer::Impl {
    explicit Impl(Options server_options)
        : options(std::move(server_options)),
          work_guard(boost::asio::make_work_guard(io_context)) {}

    Options options;
    boost::asio::io_context io_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard;
    std::unique_ptr<tcp::acceptor> acceptor;
    PacketHandler packet_handler;
    DisconnectHandler disconnect_handler;
    std::atomic<std::uint64_t> next_connection_id{1};
    std::atomic_bool stopping{false};
    std::mutex sessions_mutex;
    std::condition_variable sessions_cv;
    std::unordered_map<std::uint64_t, std::shared_ptr<TransportServer::TransportSession>> sessions;
    std::vector<std::thread> threads;
};

class TransportServer::TransportSession : public std::enable_shared_from_this<TransportSession> {
public:
    TransportSession(Impl& impl, tcp::socket socket, std::uint64_t connection_id, std::string peer_address)
        : impl_(impl),
          socket_(std::move(socket)),
          strand_(boost::asio::make_strand(impl_.io_context)),
          timer_(socket_.get_executor()),
          connection_id_(connection_id),
          peer_address_(std::move(peer_address)) {}

    void Start() {
        auto self = shared_from_this();
        boost::asio::dispatch(strand_, [self] {
            self->RefreshDeadline();
            self->ReadHeader();
        });
    }

    void Stop() {
        auto self = shared_from_this();
        boost::asio::post(strand_, [self] {
            self->BeginShutdown();
        });
    }

    void ForceClose() {
        auto self = shared_from_this();
        boost::asio::post(strand_, [self] {
            self->Close();
        });
    }

private:
    void RefreshDeadline() {
        if (impl_.options.idle_timeout_ms <= 0 || closed_) {
            return;
        }

        timer_.expires_after(std::chrono::milliseconds(impl_.options.idle_timeout_ms));
        auto self = shared_from_this();
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

        auto self = shared_from_this();
        boost::asio::async_read(socket_,
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

        auto self = shared_from_this();
        boost::asio::async_read(socket_,
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
            auto self = shared_from_this();
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

        auto self = shared_from_this();
        boost::asio::async_write(socket_,
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
        socket_.shutdown(tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
        impl_.disconnect_handler ? impl_.disconnect_handler(connection_id_) : void();
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

    Impl& impl_;
    tcp::socket socket_;
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

    if (on_stopping) {
        on_stopping();
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

    std::vector<std::shared_ptr<TransportSession>> sessions;
    {
        std::lock_guard lock(impl_->sessions_mutex);
        for (const auto& [connection_id, session] : impl_->sessions) {
            (void)connection_id;
            sessions.push_back(session);
        }
    }

    for (const auto& session : sessions) {
        if (session) {
            session->Stop();
        }
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(0, impl_->options.shutdown_grace_ms));
    {
        std::unique_lock lock(impl_->sessions_mutex);
        impl_->sessions_cv.wait_until(lock, deadline, [this] {
            return impl_->sessions.empty();
        });
    }

    sessions.clear();
    {
        std::lock_guard lock(impl_->sessions_mutex);
        for (const auto& [connection_id, session] : impl_->sessions) {
            (void)connection_id;
            sessions.push_back(session);
        }
    }
    for (const auto& session : sessions) {
        if (session) {
            session->ForceClose();
        }
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
            std::string peer_address = endpoint_error
                                           ? "unknown"
                                           : endpoint.address().to_string() + ':' + std::to_string(endpoint.port());
            const auto connection_id = impl_->next_connection_id.fetch_add(1);
            auto session = std::make_shared<TransportSession>(std::ref(*impl_), std::move(socket), connection_id, peer_address);
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
