#include "runtime/transport/transport_client.h"
#include "runtime/transport/transport_server.h"
#include "tests/tls_test_materials.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

class TempDirectory {
public:
    TempDirectory() {
        char pattern[] = "/tmp/mobile-game-backend-tls-test-XXXXXX";
        const auto* created = mkdtemp(pattern);
        if (created != nullptr) {
            path_ = created;
        }
    }

    ~TempDirectory() {
        if (!path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    [[nodiscard]] bool valid() const { return !path_.empty(); }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

int ReservePort() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return -1;
    }

    socklen_t address_length = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
        close(fd);
        return -1;
    }

    const auto port = ntohs(address.sin_port);
    close(fd);
    return port;
}

struct TestServer {
    std::unique_ptr<framework::transport::TransportServer> server;
    std::atomic_bool running{true};
    std::thread thread;
};

bool StartTlsEchoServer(const std::filesystem::path& fixture_dir, int port, TestServer* instance, std::string* error_message) {
    if (instance == nullptr) {
        if (error_message != nullptr) {
            *error_message = "test server instance is null";
        }
        return false;
    }

    framework::transport::TransportServer::Options options;
    options.io_threads = 1;
    options.idle_timeout_ms = 1000;
    options.tls.enabled = true;
    options.tls.cert_file = (fixture_dir / "server.crt").string();
    options.tls.key_file = (fixture_dir / "server.key").string();
    instance->server = std::make_unique<framework::transport::TransportServer>(options);
    instance->server->SetPacketHandler([](const framework::transport::TransportInbound& inbound,
                                          framework::transport::ResponseCallback respond) {
        respond(inbound.packet);
    });

    if (!instance->server->Start("127.0.0.1", port, error_message)) {
        return false;
    }

    instance->thread = std::thread([instance] {
        instance->server->Run([instance] { return instance->running.load(); });
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
}

void StopTlsEchoServer(TestServer* instance) {
    if (instance == nullptr) {
        return;
    }
    instance->running.store(false);
    if (instance->server != nullptr) {
        instance->server->Stop();
    }
    if (instance->thread.joinable()) {
        instance->thread.join();
    }
}

common::net::Packet BuildRequestPacket() {
    common::net::Packet packet;
    packet.header.request_id = 42;
    packet.header.msg_id = 7;
    packet.body = "trusted-tls-payload";
    packet.header.body_len = static_cast<std::uint32_t>(packet.body.size());
    return packet;
}

}  // namespace

int main() {
    TempDirectory temp_directory;
    if (!Expect(temp_directory.valid(), "expected temporary directory to be created")) {
        return 1;
    }
    if (!Expect(test::tls::WriteFixtureSet(temp_directory.path()), "expected TLS fixture files to be written")) {
        return 1;
    }

    const auto port = ReservePort();
    if (!Expect(port > 0, "expected to reserve a TCP port")) {
        return 1;
    }

    TestServer server;
    std::string error_message;
    if (!Expect(StartTlsEchoServer(temp_directory.path(), port, &server, &error_message),
                "expected TLS echo server to start: " + error_message)) {
        return 1;
    }

    const auto request = BuildRequestPacket();

    framework::transport::TlsOptions trusted_options;
    trusted_options.enabled = true;
    trusted_options.ca_file = (temp_directory.path() / "ca.pem").string();
    trusted_options.server_name = "localhost";
    trusted_options.verify_peer = true;

    framework::transport::TransportClient trusted_client("127.0.0.1", port, 1000, trusted_options);
    common::net::Packet response;
    error_message.clear();
    if (!Expect(trusted_client.SendAndReceive(request, &response, &error_message),
                "expected trusted TLS request to succeed: " + error_message)) {
        StopTlsEchoServer(&server);
        return 1;
    }
    if (!Expect(response.header.request_id == request.header.request_id, "expected response request_id to round-trip")) {
        StopTlsEchoServer(&server);
        return 1;
    }
    if (!Expect(response.body == request.body, "expected response body to round-trip")) {
        StopTlsEchoServer(&server);
        return 1;
    }

    framework::transport::TlsOptions wrong_ca_options = trusted_options;
    wrong_ca_options.ca_file = (temp_directory.path() / "wrong-ca.pem").string();
    framework::transport::TransportClient wrong_ca_client("127.0.0.1", port, 1000, wrong_ca_options);
    response = {};
    error_message.clear();
    if (!Expect(!wrong_ca_client.SendAndReceive(request, &response, &error_message),
                "expected untrusted CA request to fail")) {
        StopTlsEchoServer(&server);
        return 1;
    }

    framework::transport::TlsOptions wrong_name_options = trusted_options;
    wrong_name_options.server_name = "mismatch.example";
    framework::transport::TransportClient wrong_name_client("127.0.0.1", port, 1000, wrong_name_options);
    response = {};
    error_message.clear();
    if (!Expect(!wrong_name_client.SendAndReceive(request, &response, &error_message),
                "expected hostname mismatch request to fail")) {
        StopTlsEchoServer(&server);
        return 1;
    }

    StopTlsEchoServer(&server);
    return 0;
}
