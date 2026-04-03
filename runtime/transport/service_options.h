#pragma once

#include <string>

namespace framework::runtime {

struct ServiceCliOptions {
    std::string service_name;
    std::string config_path;
    bool check_only = false;
    bool show_version = false;
};

ServiceCliOptions ParseServiceOptions(int argc,
                                      char* argv[],
                                      std::string default_service_name,
                                      std::string default_config_path);

}  // namespace framework::runtime
