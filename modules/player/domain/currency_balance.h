#pragma once

#include <cstdint>
#include <string>

namespace common::model {

struct CurrencyBalance {
    std::string currency_type;
    std::int64_t amount = 0;
};

}  // namespace common::model
