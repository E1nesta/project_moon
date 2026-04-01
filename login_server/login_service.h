#pragma once

#include "common/error/error_code.h"
#include "common/model/session.h"
#include "login_server/auth/account_repository.h"
#include "login_server/session/session_repository.h"

#include <cstdint>
#include <string>

namespace login_server {

struct LoginRequest {
    std::string account_name;
    std::string password;
};

struct LoginResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    common::model::Session session;
    std::int64_t default_player_id = 0;
};

class LoginService {
public:
    LoginService(auth::AccountRepository& account_repository, session::SessionRepository& session_repository);

    [[nodiscard]] LoginResponse Login(const LoginRequest& request);

private:
    auth::AccountRepository& account_repository_;
    session::SessionRepository& session_repository_;
};

}  // namespace login_server
