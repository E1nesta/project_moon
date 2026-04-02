#include "common/security/password_hasher.h"

#include <openssl/evp.h>

#include <cstddef>
#include <sstream>
#include <vector>

namespace common::security {

namespace {

constexpr char kSchemePrefix[] = "pbkdf2_sha256";
constexpr int kDerivedKeyLength = 32;

}  // namespace

std::string PasswordHasher::HashPassword(const std::string& password,
                                         const std::string& salt,
                                         int iterations) {
    std::vector<unsigned char> derived_key(static_cast<std::size_t>(kDerivedKeyLength));
    if (iterations <= 0 ||
        PKCS5_PBKDF2_HMAC(password.c_str(),
                          static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char*>(salt.data()),
                          static_cast<int>(salt.size()),
                          iterations,
                          EVP_sha256(),
                          kDerivedKeyLength,
                          derived_key.data()) != 1) {
        return {};
    }

    return ToHex(derived_key.data(), derived_key.size());
}

bool PasswordHasher::VerifyPassword(const std::string& password, const std::string& encoded_hash) {
    const auto first = encoded_hash.find('$');
    const auto second = encoded_hash.find('$', first == std::string::npos ? first : first + 1);
    const auto third = encoded_hash.find('$', second == std::string::npos ? second : second + 1);
    if (first == std::string::npos || second == std::string::npos || third == std::string::npos) {
        return false;
    }

    if (encoded_hash.substr(0, first) != kSchemePrefix) {
        return false;
    }

    int iterations = 0;
    try {
        iterations = std::stoi(encoded_hash.substr(first + 1, second - first - 1));
    } catch (...) {
        return false;
    }

    const auto salt_hex = encoded_hash.substr(second + 1, third - second - 1);
    const auto expected_hash = encoded_hash.substr(third + 1);
    const auto salt = FromHex(salt_hex);
    if (!salt.has_value()) {
        return false;
    }

    return HashPassword(password, *salt, iterations) == expected_hash;
}

std::optional<std::string> PasswordHasher::BuildEncodedHash(const std::string& password,
                                                            const std::string& salt,
                                                            int iterations) {
    const auto hashed = HashPassword(password, salt, iterations);
    if (hashed.empty()) {
        return std::nullopt;
    }

    std::ostringstream output;
    output << kSchemePrefix << '$' << iterations << '$'
           << ToHex(reinterpret_cast<const unsigned char*>(salt.data()), salt.size())
           << '$' << hashed;
    return output.str();
}

std::string PasswordHasher::ToHex(const unsigned char* data, std::size_t size) {
    static constexpr char kHexDigits[] = "0123456789abcdef";

    std::string output;
    output.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        output.push_back(kHexDigits[(data[index] >> 4U) & 0x0FU]);
        output.push_back(kHexDigits[data[index] & 0x0FU]);
    }
    return output;
}

std::optional<std::string> PasswordHasher::FromHex(const std::string& hex) {
    if ((hex.size() % 2U) != 0U) {
        return std::nullopt;
    }

    auto decode = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };

    std::string output;
    output.reserve(hex.size() / 2U);
    for (std::size_t index = 0; index < hex.size(); index += 2U) {
        const auto high = decode(hex[index]);
        const auto low = decode(hex[index + 1U]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        output.push_back(static_cast<char>((high << 4) | low));
    }

    return output;
}

}  // namespace common::security
