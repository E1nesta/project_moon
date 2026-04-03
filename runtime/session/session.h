#pragma once

#include <cstdint>
#include <string>

namespace common::model {

struct Session {
    std::string session_id;
    std::int64_t account_id = 0;
    std::int64_t player_id = 0;
    std::int64_t created_at_epoch_seconds = 0;
    std::int64_t expires_at_epoch_seconds = 0;
};

}  // namespace common::model
