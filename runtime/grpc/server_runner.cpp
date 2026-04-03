#include "runtime/grpc/server_runner.h"

#include <grpcpp/security/server_credentials.h>

namespace framework::grpc {

ServerRunner::ServerRunner(std::string host, int port) : host_(std::move(host)), port_(port) {}

bool ServerRunner::Start(::grpc::Service* service, std::string* error_message) {
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
    builder.AddListeningPort(host_ + ":" + std::to_string(port_), ::grpc::InsecureServerCredentials(), &selected_port_);
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
