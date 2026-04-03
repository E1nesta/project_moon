#pragma once

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <atomic>
#include <memory>
#include <string>

namespace framework::grpc {

class ServerRunner {
public:
    ServerRunner(std::string host, int port);

    ServerRunner(const ServerRunner&) = delete;
    ServerRunner& operator=(const ServerRunner&) = delete;

    bool Start(::grpc::Service* service, std::string* error_message = nullptr);
    [[nodiscard]] int Port() const;
    void Wait();
    void Shutdown();

private:
    std::string host_;
    int port_ = 0;
    int selected_port_ = 0;
    std::atomic_bool shutdown_requested_{false};
    std::unique_ptr<::grpc::Server> server_;
};

}  // namespace framework::grpc
