#pragma once

#include <string>

namespace common::bootstrap {

struct ServiceOptions {
    std::string service_name;
    std::string config_path;
    bool check_only = false;
};

int RunService(const ServiceOptions& options);

}  // namespace common::bootstrap

