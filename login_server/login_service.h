#pragma once

#include "common/error/error_code.h"
#include "common/model/session.h"
#include "login_server/auth/account_repository.h"
#include "login_server/session/session_repository.h"

#include <cstdint>
#include <optional>
#include <string>

namespace login_server {

// Application model for the login use case.
struct LoginRequest {
    std::string account_name;
    std::string password;
};

// Application result returned by the login application service.
struct LoginResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    common::model::Session session;
    std::int64_t default_player_id = 0;
};

// Application service that orchestrates authentication and session creation.
class LoginService {
public:
    LoginService(auth::AccountRepository& account_repository, session::SessionRepository& session_repository);

    [[nodiscard]] LoginResponse Login(const LoginRequest& request);

private:
    [[nodiscard]] std::optional<common::model::Account> LoadAccount(const std::string& account_name) const;
    [[nodiscard]] std::optional<LoginResponse> ValidateAccount(const common::model::Account& account,
                                                               const std::string& password) const;
    [[nodiscard]] LoginResponse BuildSuccessResponse(const common::model::Account& account) const;

    auth::AccountRepository& account_repository_;
    session::SessionRepository& session_repository_;
};

}  // namespace login_server
