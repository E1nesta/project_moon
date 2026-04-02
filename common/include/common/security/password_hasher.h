#pragma once

#include <optional>
#include <string>

namespace common::security {

class PasswordHasher {
public:
    [[nodiscard]] static std::string HashPassword(const std::string& password,
                                                  const std::string& salt,
                                                  int iterations = 100000);
    [[nodiscard]] static bool VerifyPassword(const std::string& password, const std::string& encoded_hash);
    [[nodiscard]] static std::optional<std::string> BuildEncodedHash(const std::string& password,
                                                                     const std::string& salt,
                                                                     int iterations = 100000);

private:
    [[nodiscard]] static std::string ToHex(const unsigned char* data, std::size_t size);
    [[nodiscard]] static std::optional<std::string> FromHex(const std::string& hex);
};

}  // namespace common::security
