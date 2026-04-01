#pragma once

#include <cstdint>
#include <string>

namespace common::net {

struct RequestContext {
    std::string trace_id;
    std::uint64_t request_id = 0;
    std::string session_id;
    std::int64_t player_id = 0;
};

}  // namespace common::net
