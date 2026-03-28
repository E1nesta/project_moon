#include "common/bootstrap/service_app.h"

#include <string>

namespace {

common::bootstrap::ServiceOptions ParseOptions(int argc, char* argv[]) {
    common::bootstrap::ServiceOptions options;
    options.service_name = "login_server";
    options.config_path = "configs/login_server.conf";
    options.check_only = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (arg == "--check") {
            options.check_only = true;
        }
    }

    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    return common::bootstrap::RunService(ParseOptions(argc, argv));
}
