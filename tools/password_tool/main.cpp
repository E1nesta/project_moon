#include "runtime/foundation/build/build_info.h"
#include "runtime/foundation/security/password_hasher.h"

#include <iostream>
#include <string>

namespace {

struct Options {
    std::string password;
    std::string salt = "starter-kit-salt";
    int iterations = 100000;
    bool show_version = false;
};

Options ParseOptions(int argc, char* argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--password" && index + 1 < argc) {
            options.password = argv[++index];
        } else if (arg == "--salt" && index + 1 < argc) {
            options.salt = argv[++index];
        } else if (arg == "--iterations" && index + 1 < argc) {
            options.iterations = std::stoi(argv[++index]);
        } else if (arg == "--version") {
            options.show_version = true;
        }
    }
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto options = ParseOptions(argc, argv);
    if (options.show_version) {
        std::cout << common::build::Version() << '\n';
        return 0;
    }

    if (options.password.empty()) {
        std::cerr << "missing --password\n";
        return 1;
    }

    const auto encoded_hash = common::security::PasswordHasher::BuildEncodedHash(
        options.password, options.salt, options.iterations);
    if (!encoded_hash.has_value()) {
        std::cerr << "failed to build password hash\n";
        return 1;
    }

    std::cout << *encoded_hash << '\n';
    return 0;
}
