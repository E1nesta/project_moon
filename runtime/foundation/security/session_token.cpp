#include "runtime/foundation/security/session_token.h"

#include <openssl/rand.h>

#include <cstddef>
#include <vector>

namespace common::security {

namespace {

std::string ToHex(const unsigned char* data, std::size_t size) {
    static constexpr char kHexDigits[] = "0123456789abcdef";

    std::string output;
    output.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        output.push_back(kHexDigits[(data[index] >> 4U) & 0x0FU]);
        output.push_back(kHexDigits[data[index] & 0x0FU]);
    }
    return output;
}

}  // namespace

std::optional<std::string> SessionToken::GenerateHex(std::size_t num_bytes) {
    if (num_bytes == 0) {
        return std::nullopt;
    }

    std::vector<unsigned char> bytes(num_bytes);
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        return std::nullopt;
    }

    return ToHex(bytes.data(), bytes.size());
}

}  // namespace common::security
