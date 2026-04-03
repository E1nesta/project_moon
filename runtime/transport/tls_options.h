#pragma once

#include "runtime/foundation/config/simple_config.h"

#include <string>

namespace framework::transport {

struct TlsOptions {
    bool enabled = false;
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    std::string server_name;
    bool verify_peer = false;
};

[[nodiscard]] TlsOptions ReadTlsOptions(const common::config::SimpleConfig& config,
                                        const std::string& prefix);

}  // namespace framework::transport
