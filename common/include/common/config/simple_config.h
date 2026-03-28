#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace common::config {

class SimpleConfig {
public:
    bool LoadFromFile(const std::string& path);

    [[nodiscard]] bool Contains(const std::string& key) const;
    [[nodiscard]] std::string GetString(const std::string& key, const std::string& default_value = "") const;
    [[nodiscard]] int GetInt(const std::string& key, int default_value = 0) const;
    [[nodiscard]] bool GetBool(const std::string& key, bool default_value = false) const;

    [[nodiscard]] const std::unordered_map<std::string, std::string>& Values() const;

private:
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace common::config

