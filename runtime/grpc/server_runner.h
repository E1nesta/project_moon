#pragma once

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <atomic>
#include <memory>
#include <string>

namespace framework::grpc {

struct TlsServerCredentialsOptions {
    std::string cert_chain_file;
    std::string private_key_file;
    std::string client_ca_file;
    bool require_client_auth = false;
};

class ServerRunner {
public:
    ServerRunner(std::string host, int port);

    ServerRunner(const ServerRunner&) = delete;
    ServerRunner& operator=(const ServerRunner&) = delete;

    bool Start(::grpc::Service* service, std::string* error_message = nullptr);
    bool Start(::grpc::Service* service,
               const TlsServerCredentialsOptions& tls_options,
               std::string* error_message = nullptr);
    [[nodiscard]] int Port() const;
    void Wait();
    void Shutdown();

private:
    bool StartWithCredentials(::grpc::Service* service,
                              std::shared_ptr<::grpc::ServerCredentials> credentials,
                              std::string* error_message);

    std::string host_;
    int port_ = 0;
    int selected_port_ = 0;
    std::atomic_bool shutdown_requested_{false};
    std::unique_ptr<::grpc::Server> server_;
};

}  // namespace framework::grpc
