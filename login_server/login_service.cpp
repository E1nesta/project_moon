#include "login_server/login_service.h"

#include "common/security/password_hasher.h"

namespace login_server {

namespace {

LoginResponse BuildLoginError(common::error::ErrorCode error_code, std::string error_message) {
    return {false, error_code, std::move(error_message), {}, 0};
}

LoginResponse BuildLoginSuccess(common::model::Session session, std::int64_t default_player_id) {
    auto response = LoginResponse{};
    response.success = true;
    response.error_code = common::error::ErrorCode::kOk;
    response.default_player_id = default_player_id;
    response.session = std::move(session);
    return response;
}

}  // namespace

LoginService::LoginService(auth::AccountRepository& account_repository, session::SessionRepository& session_repository)
    : account_repository_(account_repository), session_repository_(session_repository) {}

LoginResponse LoginService::Login(const LoginRequest& request) {
    const auto account = LoadAccount(request.account_name);
    if (!account.has_value()) {
        return BuildLoginError(common::error::ErrorCode::kAccountNotFound, "account not found");
    }

    if (const auto validation = ValidateAccount(*account, request.password); validation.has_value()) {
        return *validation;
    }

    return BuildSuccessResponse(*account);
}

std::optional<common::model::Account> LoginService::LoadAccount(const std::string& account_name) const {
    return account_repository_.FindByName(account_name);
}

std::optional<LoginResponse> LoginService::ValidateAccount(const common::model::Account& account,
                                                           const std::string& password) const {
    if (!account.enabled) {
        return BuildLoginError(common::error::ErrorCode::kAccountDisabled, "account disabled");
    }

    if (!common::security::PasswordHasher::VerifyPassword(password, account.password_hash)) {
        return BuildLoginError(common::error::ErrorCode::kInvalidPassword, "invalid password");
    }

    return std::nullopt;
}

LoginResponse LoginService::BuildSuccessResponse(const common::model::Account& account) const {
    return BuildLoginSuccess(
        session_repository_.Create(account.account_id, account.default_player_id), account.default_player_id);
}

}  // namespace login_server
