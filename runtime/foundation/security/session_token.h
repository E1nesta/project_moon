#pragma once

#include <optional>
#include <string>

namespace common::security {

// Generates opaque session identifiers for externally visible session tokens.
class SessionToken {
public:
    [[nodiscard]] static std::optional<std::string> GenerateHex(std::size_t num_bytes = 32);
};

}  // namespace common::security
