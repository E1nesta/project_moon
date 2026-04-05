#include "runtime/grpc/channel_factory.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

#include <fstream>
#include <sstream>

namespace framework::grpc {

namespace {

bool ReadFile(const std::string& path, std::string* contents, std::string* error_message) {
    if (contents == nullptr) {
        if (error_message != nullptr) {
            *error_message = "grpc tls output buffer is null";
        }
        return false;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        if (error_message != nullptr) {
            *error_message = "failed to open grpc tls file: " + path;
        }
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    *contents = buffer.str();
    if (contents->empty()) {
        if (error_message != nullptr) {
            *error_message = "grpc tls file is empty: " + path;
        }
        return false;
    }
    return true;
}

}  // namespace

std::string BuildAddress(const common::config::SimpleConfig& config, const std::string& prefix) {
    const auto host = config.GetString(prefix + "host", "127.0.0.1");
    const auto port = config.GetInt(prefix + "port", 0);
    return host + ":" + std::to_string(port);
}

std::shared_ptr<::grpc::Channel> CreateInsecureChannel(const common::config::SimpleConfig& config,
                                                       const std::string& prefix) {
    return ::grpc::CreateChannel(BuildAddress(config, prefix), ::grpc::InsecureChannelCredentials());
}

std::shared_ptr<::grpc::Channel> CreateTlsChannel(const std::string& address,
                                                  const TlsChannelOptions& options,
                                                  std::string* error_message) {
    if (options.root_cert_file.empty() || options.cert_chain_file.empty() || options.private_key_file.empty()) {
        if (error_message != nullptr) {
            *error_message = "grpc tls root_cert_file, cert_chain_file and private_key_file are required";
        }
        return nullptr;
    }

    std::string root_certs;
    std::string cert_chain;
    std::string private_key;
    if (!ReadFile(options.root_cert_file, &root_certs, error_message) ||
        !ReadFile(options.cert_chain_file, &cert_chain, error_message) ||
        !ReadFile(options.private_key_file, &private_key, error_message)) {
        return nullptr;
    }

    ::grpc::SslCredentialsOptions ssl_options;
    ssl_options.pem_root_certs = root_certs;
    ssl_options.pem_cert_chain = cert_chain;
    ssl_options.pem_private_key = private_key;

    ::grpc::ChannelArguments channel_arguments;
    if (!options.server_name.empty()) {
        channel_arguments.SetSslTargetNameOverride(options.server_name);
    }
    return ::grpc::CreateCustomChannel(address, ::grpc::SslCredentials(ssl_options), channel_arguments);
}

}  // namespace framework::grpc
