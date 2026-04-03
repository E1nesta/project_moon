#include "runtime/grpc/channel_factory.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

namespace framework::grpc {

std::string BuildAddress(const common::config::SimpleConfig& config, const std::string& prefix) {
    const auto host = config.GetString(prefix + "host", "127.0.0.1");
    const auto port = config.GetInt(prefix + "port", 0);
    return host + ":" + std::to_string(port);
}

std::shared_ptr<::grpc::Channel> CreateInsecureChannel(const common::config::SimpleConfig& config,
                                                       const std::string& prefix) {
    return ::grpc::CreateChannel(BuildAddress(config, prefix), ::grpc::InsecureChannelCredentials());
}

}  // namespace framework::grpc
