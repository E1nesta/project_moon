#include "common/config/simple_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace common::config {

namespace {

std::string Trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

}  // namespace

bool SimpleConfig::LoadFromFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    values_.clear();

    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        std::string key = Trim(line.substr(0, separator));
        std::string value = Trim(line.substr(separator + 1));
        if (!key.empty()) {
            values_[std::move(key)] = std::move(value);
        }
    }

    return true;
}

bool SimpleConfig::Contains(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::string SimpleConfig::GetString(const std::string& key, const std::string& default_value) const {
    const auto iter = values_.find(key);
    return iter == values_.end() ? default_value : iter->second;
}

int SimpleConfig::GetInt(const std::string& key, int default_value) const {
    const auto iter = values_.find(key);
    if (iter == values_.end()) {
        return default_value;
    }

    try {
        return std::stoi(iter->second);
    } catch (...) {
        return default_value;
    }
}

bool SimpleConfig::GetBool(const std::string& key, bool default_value) const {
    const auto value = GetString(key);
    if (value.empty()) {
        return default_value;
    }

    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }

    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }

    return default_value;
}

const std::unordered_map<std::string, std::string>& SimpleConfig::Values() const {
    return values_;
}

}  // namespace common::config
