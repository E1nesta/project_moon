#include "framework/runtime/service_options.h"

namespace framework::runtime {

ServiceCliOptions ParseServiceOptions(int argc,
                                      char* argv[],
                                      std::string default_service_name,
                                      std::string default_config_path) {
    ServiceCliOptions options;
    options.service_name = std::move(default_service_name);
    options.config_path = std::move(default_config_path);

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (arg == "--check") {
            options.check_only = true;
        } else if (arg == "--version") {
            options.show_version = true;
        }
    }

    return options;
}

}  // namespace framework::runtime
