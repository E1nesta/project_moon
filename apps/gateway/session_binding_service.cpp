#include "apps/gateway/session_binding_service.h"

namespace services::gateway {

SessionBindingService::SessionBindingService(common::session::SessionReader& session_reader)
    : session_reader_(session_reader) {}

SessionBindingService::Result SessionBindingService::ValidateOrRestore(
    std::uint64_t connection_id,
    common::net::RequestContext* context) {
    if (context == nullptr) {
        return {Status::kInvalid, "request context is null"};
    }

    {
        std::lock_guard lock(mutex_);
        const auto binding_iter = client_bindings_.find(connection_id);
        if (binding_iter != client_bindings_.end()) {
            if (!context->auth_token.empty() && binding_iter->second.auth_token != context->auth_token) {
                return {Status::kInvalid, "connection session binding mismatch"};
            }

            if (context->player_id != 0 && binding_iter->second.player_id != context->player_id) {
                return {Status::kInvalid, "connection session binding mismatch"};
            }

            if (context->account_id != 0 && binding_iter->second.account_id != context->account_id) {
                return {Status::kInvalid, "connection session binding mismatch"};
            }

            if (context->auth_token.empty()) {
                context->auth_token = binding_iter->second.auth_token;
            }
            context->player_id = binding_iter->second.player_id;
            context->account_id = binding_iter->second.account_id;
            return {Status::kBound, {}};
        }
    }

    const auto restored_binding = RestoreBindingFromSession(context);
    if (!restored_binding.has_value()) {
        return {Status::kInvalid, "session restore failed"};
    }

    std::lock_guard lock(mutex_);
    client_bindings_[connection_id] = *restored_binding;
    return {Status::kRestored, {}};
}

void SessionBindingService::Bind(std::uint64_t connection_id,
                                 const std::string& auth_token,
                                 std::int64_t player_id,
                                 std::int64_t account_id) {
    std::lock_guard lock(mutex_);
    client_bindings_[connection_id] = {auth_token, player_id, account_id};
}

void SessionBindingService::Unbind(std::uint64_t connection_id) {
    std::lock_guard lock(mutex_);
    client_bindings_.erase(connection_id);
}

std::optional<SessionBindingService::ClientBinding> SessionBindingService::RestoreBindingFromSession(
    common::net::RequestContext* context) const {
    if (context == nullptr || context->auth_token.empty()) {
        return std::nullopt;
    }

    const auto session = session_reader_.FindById(context->auth_token);
    if (!session.has_value()) {
        return std::nullopt;
    }

    if (context->player_id != 0 && session->player_id != context->player_id) {
        return std::nullopt;
    }

    if (context->account_id != 0 && session->account_id != context->account_id) {
        return std::nullopt;
    }

    context->player_id = session->player_id;
    context->account_id = session->account_id;
    return ClientBinding{session->session_id, session->player_id, session->account_id};
}

}  // namespace services::gateway
