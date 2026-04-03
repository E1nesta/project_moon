#include "tests/tls_test_materials.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <cstdlib>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
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
        char pattern[] = "/tmp/mobile-game-backend-service-check-modes-XXXXXX";
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

std::string ShellQuote(const std::string& text) {
    std::string quoted = "'";
    for (const char character : text) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

int RunCommand(const std::string& command) {
    const int status = std::system(command.c_str());
    if (status < 0) {
        return status;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return status;
}

int ReserveClosedPort() {
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

bool WriteConfig(const std::filesystem::path& path,
                 const std::filesystem::path& fixture_dir,
                 bool include_tls_key,
                 int mysql_port,
                 int redis_port,
                 int login_port,
                 int player_port,
                 int dungeon_port) {
    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }

    output << "service.name=gateway_server\n";
    output << "service.instance_id=gateway_test\n";
    output << "runtime.environment=dev\n";
    output << "service.listen.host=127.0.0.1\n";
    output << "service.listen.port=7000\n";
    output << "security.trusted_gateway.shared_secret=test-shared-secret\n";
    output << "security.trusted_gateway.max_clock_skew_ms=10000\n";
    output << "transport.tls.enabled=true\n";
    output << "transport.tls.cert_file=" << (fixture_dir / "server.crt").string() << '\n';
    output << "transport.tls.key_file=";
    if (include_tls_key) {
        output << (fixture_dir / "server.key").string();
    } else {
        output << (fixture_dir / "missing.key").string();
    }
    output << '\n';
    output << "storage.mysql.host=127.0.0.1\n";
    output << "storage.mysql.port=" << mysql_port << '\n';
    output << "storage.mysql.user=game\n";
    output << "storage.mysql.password=gamepass\n";
    output << "storage.mysql.database=game_backend\n";
    output << "storage.redis.host=127.0.0.1\n";
    output << "storage.redis.port=" << redis_port << '\n';
    output << "storage.redis.timeout_ms=100\n";
    output << "upstream.timeout_ms=100\n";
    output << "gateway.forward_workers=4\n";
    output << "gateway.forward_queue_limit=1024\n";
    output << "upstream.login.host=127.0.0.1\n";
    output << "upstream.login.port=" << login_port << '\n';
    output << "upstream.player.host=127.0.0.1\n";
    output << "upstream.player.port=" << player_port << '\n';
    output << "upstream.dungeon.host=127.0.0.1\n";
    output << "upstream.dungeon.port=" << dungeon_port << '\n';
    return true;
}

}  // namespace

int main(int /*argc*/, char* argv[]) {
    TempDirectory temp_directory;
    if (!Expect(temp_directory.valid(), "expected temporary directory to be created")) {
        return 1;
    }
    if (!Expect(test::tls::WriteFixtureSet(temp_directory.path()), "expected TLS fixture files to be written")) {
        return 1;
    }

    const auto mysql_port = ReserveClosedPort();
    const auto redis_port = ReserveClosedPort();
    const auto login_port = ReserveClosedPort();
    const auto player_port = ReserveClosedPort();
    const auto dungeon_port = ReserveClosedPort();
    if (!Expect(mysql_port > 0 && redis_port > 0 && login_port > 0 && player_port > 0 && dungeon_port > 0,
                "expected closed dependency ports to be reserved")) {
        return 1;
    }

    const auto build_dir = std::filesystem::canonical(std::filesystem::path(argv[0])).parent_path();
    const auto service_check = build_dir / "service_check";
    const auto valid_config = temp_directory.path() / "service_check_valid.conf";
    const auto invalid_config = temp_directory.path() / "service_check_invalid.conf";

    if (!Expect(WriteConfig(valid_config,
                            temp_directory.path(),
                            true,
                            mysql_port,
                            redis_port,
                            login_port,
                            player_port,
                            dungeon_port),
                "expected valid config to be written")) {
        return 1;
    }
    if (!Expect(WriteConfig(invalid_config,
                            temp_directory.path(),
                            false,
                            mysql_port,
                            redis_port,
                            login_port,
                            player_port,
                            dungeon_port),
                "expected invalid config to be written")) {
        return 1;
    }

    if (!Expect(
            RunCommand(ShellQuote(service_check.string()) + " --config " + ShellQuote(valid_config.string()) +
                       " --mode config") == 0,
            "expected config mode to pass local validation without dependencies")) {
        return 1;
    }
    if (!Expect(
            RunCommand(ShellQuote(service_check.string()) + " --config " + ShellQuote(valid_config.string()) +
                       " --mode live") == 0,
            "expected live mode to ignore unavailable mysql redis and upstreams")) {
        return 1;
    }
    if (!Expect(
            RunCommand(ShellQuote(service_check.string()) + " --config " + ShellQuote(valid_config.string()) +
                       " --mode ready") != 0,
            "expected ready mode to fail when dependencies are unavailable")) {
        return 1;
    }
    if (!Expect(
            RunCommand(ShellQuote(service_check.string()) + " --config " + ShellQuote(valid_config.string())) != 0,
            "expected default ready mode to fail when dependencies are unavailable")) {
        return 1;
    }
    if (!Expect(
            RunCommand(ShellQuote(service_check.string()) + " --config " + ShellQuote(invalid_config.string()) +
                       " --mode config") != 0,
            "expected config mode to fail when TLS files are missing")) {
        return 1;
    }
    if (!Expect(
            RunCommand(ShellQuote(service_check.string()) + " --config " + ShellQuote(invalid_config.string()) +
                       " --mode live") != 0,
            "expected live mode to fail when TLS files are missing")) {
        return 1;
    }

    return 0;
}
