#pragma once

#include "common/net/request_context.h"
#include "login_server/session/session_repository.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace gateway {

class SessionBindingAuthorizer {
public:
    struct ClientBinding {
        std::string session_id;
        std::int64_t player_id = 0;
    };

    enum class Status {
        kBound,
        kRestored,
        kInvalid,
    };

    struct Result {
        Status status = Status::kInvalid;
        std::string reason;
    };

    explicit SessionBindingAuthorizer(login_server::session::SessionRepository& session_repository);

    [[nodiscard]] Result ValidateOrRestore(std::uint64_t connection_id,
                                           const common::net::RequestContext& context);
    void Bind(std::uint64_t connection_id, const std::string& session_id, std::int64_t player_id);
    void Unbind(std::uint64_t connection_id);

private:
    [[nodiscard]] std::optional<ClientBinding> RestoreBindingFromSession(const common::net::RequestContext& context) const;

    login_server::session::SessionRepository& session_repository_;
    std::unordered_map<std::uint64_t, ClientBinding> client_bindings_;
};

}  // namespace gateway
