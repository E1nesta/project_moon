#pragma once

#include "runtime/foundation/config/simple_config.h"

#include <grpcpp/channel.h>

#include <memory>
#include <string>

namespace framework::grpc {

std::string BuildAddress(const common::config::SimpleConfig& config, const std::string& prefix);

std::shared_ptr<::grpc::Channel> CreateInsecureChannel(const common::config::SimpleConfig& config,
                                                       const std::string& prefix);

}  // namespace framework::grpc
