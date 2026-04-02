#include "services/gateway/session_binding_store.h"

namespace services::gateway {

SessionBindingStore::SessionBindingStore(login_server::session::SessionRepository& session_repository)
    : session_repository_(session_repository) {}

SessionBindingStore::Result SessionBindingStore::ValidateOrRestore(
    std::uint64_t connection_id,
    common::net::RequestContext* context) {
    if (context == nullptr) {
        return {Status::kInvalid, "request context is null"};
    }

    {
        std::lock_guard lock(mutex_);
        const auto binding_iter = client_bindings_.find(connection_id);
        if (binding_iter != client_bindings_.end()) {
            if (!context->session_id.empty() && binding_iter->second.session_id != context->session_id) {
                return {Status::kInvalid, "connection session binding mismatch"};
            }

            if (context->player_id != 0 && binding_iter->second.player_id != context->player_id) {
                return {Status::kInvalid, "connection session binding mismatch"};
            }

            if (context->session_id.empty()) {
                context->session_id = binding_iter->second.session_id;
            }
            context->player_id = binding_iter->second.player_id;
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

void SessionBindingStore::Bind(std::uint64_t connection_id,
                               const std::string& session_id,
                               std::int64_t player_id) {
    std::lock_guard lock(mutex_);
    client_bindings_[connection_id] = {session_id, player_id};
}

void SessionBindingStore::Unbind(std::uint64_t connection_id) {
    std::lock_guard lock(mutex_);
    client_bindings_.erase(connection_id);
}

std::optional<SessionBindingStore::ClientBinding> SessionBindingStore::RestoreBindingFromSession(
    common::net::RequestContext* context) const {
    if (context == nullptr || context->session_id.empty()) {
        return std::nullopt;
    }

    const auto session = session_repository_.FindById(context->session_id);
    if (!session.has_value()) {
        return std::nullopt;
    }

    if (context->player_id != 0 && session->player_id != context->player_id) {
        return std::nullopt;
    }

    context->player_id = session->player_id;
    return ClientBinding{session->session_id, session->player_id};
}

}  // namespace services::gateway
