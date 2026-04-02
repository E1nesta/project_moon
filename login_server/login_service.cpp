#include "login_server/login_service.h"

#include "common/security/password_hasher.h"

namespace login_server {

LoginService::LoginService(auth::AccountRepository& account_repository, session::SessionRepository& session_repository)
    : account_repository_(account_repository), session_repository_(session_repository) {}

LoginResponse LoginService::Login(const LoginRequest& request) {
    const auto account = account_repository_.FindByName(request.account_name);
    if (!account.has_value()) {
        return LoginResponse{false, common::error::ErrorCode::kAccountNotFound, "account not found", {}, 0};
    }

    if (!account->enabled) {
        return LoginResponse{false, common::error::ErrorCode::kAccountDisabled, "account disabled", {}, 0};
    }

    if (!common::security::PasswordHasher::VerifyPassword(request.password, account->password_hash)) {
        return LoginResponse{false, common::error::ErrorCode::kInvalidPassword, "invalid password", {}, 0};
    }

    auto response = LoginResponse{};
    response.success = true;
    response.error_code = common::error::ErrorCode::kOk;
    response.default_player_id = account->default_player_id;
    response.session = session_repository_.Create(account->account_id, account->default_player_id);
    return response;
}

}  // namespace login_server
