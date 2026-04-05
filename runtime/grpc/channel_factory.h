#pragma once

#include "runtime/foundation/config/simple_config.h"

#include <grpcpp/channel.h>

#include <memory>
#include <string>

namespace framework::grpc {

struct TlsChannelOptions {
    std::string root_cert_file;
    std::string cert_chain_file;
    std::string private_key_file;
    std::string server_name;
};

std::string BuildAddress(const common::config::SimpleConfig& config, const std::string& prefix);

std::shared_ptr<::grpc::Channel> CreateInsecureChannel(const common::config::SimpleConfig& config,
                                                       const std::string& prefix);
std::shared_ptr<::grpc::Channel> CreateTlsChannel(const std::string& address,
                                                  const TlsChannelOptions& options,
                                                  std::string* error_message = nullptr);

}  // namespace framework::grpc
