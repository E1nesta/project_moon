#pragma once

#include <cstdint>
#include <string>

namespace common::net {

struct RequestContext {
    std::string trace_id;
    std::uint64_t request_id = 0;
    std::string auth_token;
    std::int64_t player_id = 0;
    std::int64_t account_id = 0;
    std::int64_t gateway_timestamp_ms = 0;
    std::string gateway_signature;
};

}  // namespace common::net
