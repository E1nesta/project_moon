#pragma once

#include <cstdint>
#include <string>

namespace common::model {

struct Reward {
    std::string reward_type;
    std::int64_t amount = 0;
};

}  // namespace common::model
