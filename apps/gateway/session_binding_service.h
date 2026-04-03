#pragma once

#include "runtime/protocol/request_context.h"
#include "runtime/session/session_store.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace services::gateway {

class SessionBindingService {
public:
    struct ClientBinding {
        std::string auth_token;
        std::int64_t player_id = 0;
        std::int64_t account_id = 0;
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

    explicit SessionBindingService(common::session::SessionReader& session_reader);

    [[nodiscard]] Result ValidateOrRestore(std::uint64_t connection_id,
                                           common::net::RequestContext* context);
    void Bind(std::uint64_t connection_id,
              const std::string& auth_token,
              std::int64_t player_id,
              std::int64_t account_id);
    void Unbind(std::uint64_t connection_id);

private:
    [[nodiscard]] std::optional<ClientBinding> RestoreBindingFromSession(
        common::net::RequestContext* context) const;

    common::session::SessionReader& session_reader_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, ClientBinding> client_bindings_;
};

}  // namespace services::gateway
