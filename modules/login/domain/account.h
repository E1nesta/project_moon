#pragma once

#include <cstdint>
#include <string>

namespace common::model {

struct Account {
    std::int64_t account_id = 0;
    std::string account_name;
    std::string password_hash;
    std::int64_t default_player_id = 0;
    bool enabled = true;
    bool login_banned = false;
    bool realname_verified = false;
};

}  // namespace common::model
