#include "runtime/foundation/security/password_hasher.h"
#include "modules/login/infrastructure/in_memory_account_repository.h"
#include "modules/login/application/login_service.h"
#include "runtime/session/in_memory_session_store.h"

#include <fstream>
#include <iostream>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

}  // namespace

int main() {
    common::config::SimpleConfig config;
    common::session::InMemorySessionStore session_repository;

    const auto demo_hash = common::security::PasswordHasher::BuildEncodedHash("demo123", "login-test-salt");
    if (!Expect(demo_hash.has_value(), "failed to build demo password hash")) {
        return 1;
    }

    // Build a config-backed in-memory account repo to exercise the same loading path as demo_flow.
    const std::string config_path = "login_service_test.conf";
    {
        std::ofstream output(config_path);
        output << "demo.account_id=10001\n";
        output << "demo.account_name=demo\n";
        output << "demo.password_hash=" << *demo_hash << '\n';
        output << "demo.default_player_id=20001\n";
    }

    if (!config.LoadFromFile(config_path)) {
        std::cerr << "failed to load test config\n";
        return 1;
    }

    auto account_repository = login_server::auth::InMemoryAccountRepository::FromConfig(config);
    login_server::LoginService login_service(account_repository, session_repository);

    const auto login_success = login_service.Login({"demo", "demo123"});
    if (!Expect(login_success.success, "expected demo login to succeed")) {
        return 1;
    }
    if (!Expect(!login_success.session.session_id.empty(), "expected session id to be generated")) {
        return 1;
    }

    const auto wrong_password = login_service.Login({"demo", "bad-password"});
    if (!Expect(!wrong_password.success, "expected wrong password login to fail")) {
        return 1;
    }
    if (!Expect(wrong_password.error_code == common::error::ErrorCode::kInvalidPassword,
                "expected invalid password error code")) {
        return 1;
    }

    const auto missing_account = login_service.Login({"missing", "demo123"});
    if (!Expect(!missing_account.success, "expected missing account login to fail")) {
        return 1;
    }
    if (!Expect(missing_account.error_code == common::error::ErrorCode::kAccountNotFound,
                "expected account not found error code")) {
        return 1;
    }

    return 0;
}
