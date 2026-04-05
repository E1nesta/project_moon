#include "runtime/grpc/server_runner.h"

#include <grpcpp/security/server_credentials.h>

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

ServerRunner::ServerRunner(std::string host, int port) : host_(std::move(host)), port_(port) {}

bool ServerRunner::Start(::grpc::Service* service, std::string* error_message) {
    return StartWithCredentials(service, ::grpc::InsecureServerCredentials(), error_message);
}

bool ServerRunner::Start(::grpc::Service* service,
                         const TlsServerCredentialsOptions& tls_options,
                         std::string* error_message) {
    if (tls_options.cert_chain_file.empty() || tls_options.private_key_file.empty() || tls_options.client_ca_file.empty()) {
        if (error_message != nullptr) {
            *error_message = "grpc tls cert_chain_file, private_key_file and client_ca_file are required";
        }
        return false;
    }

    std::string cert_chain;
    std::string private_key;
    std::string client_ca;
    if (!ReadFile(tls_options.cert_chain_file, &cert_chain, error_message) ||
        !ReadFile(tls_options.private_key_file, &private_key, error_message) ||
        !ReadFile(tls_options.client_ca_file, &client_ca, error_message)) {
        return false;
    }

    ::grpc::SslServerCredentialsOptions ssl_options;
    ssl_options.pem_root_certs = client_ca;
    ssl_options.pem_key_cert_pairs.push_back({private_key, cert_chain});
    ssl_options.client_certificate_request =
        tls_options.require_client_auth
            ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
            : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;

    return StartWithCredentials(service, ::grpc::SslServerCredentials(ssl_options), error_message);
}

bool ServerRunner::StartWithCredentials(::grpc::Service* service,
                                        std::shared_ptr<::grpc::ServerCredentials> credentials,
                                        std::string* error_message) {
    if (service == nullptr) {
        if (error_message != nullptr) {
            *error_message = "grpc service is null";
        }
        return false;
    }
    if (port_ < 0) {
        if (error_message != nullptr) {
            *error_message = "grpc listen port must be non-negative";
        }
        return false;
    }

    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(host_ + ":" + std::to_string(port_), std::move(credentials), &selected_port_);
    builder.RegisterService(service);
    server_ = builder.BuildAndStart();
    if (!server_ || selected_port_ <= 0) {
        if (error_message != nullptr) {
            *error_message = "failed to start grpc server";
        }
        return false;
    }
    shutdown_requested_.store(false);
    return true;
}

int ServerRunner::Port() const {
    return selected_port_;
}

void ServerRunner::Wait() {
    if (server_) {
        server_->Wait();
    }
}

void ServerRunner::Shutdown() {
    if (server_ && !shutdown_requested_.exchange(true)) {
        server_->Shutdown();
    }
}

}  // namespace framework::grpc
