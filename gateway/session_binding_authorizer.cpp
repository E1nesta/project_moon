#include "gateway/session_binding_authorizer.h"

namespace gateway {

SessionBindingAuthorizer::SessionBindingAuthorizer(login_server::session::SessionRepository& session_repository)
    : session_repository_(session_repository) {}

SessionBindingAuthorizer::Result SessionBindingAuthorizer::ValidateOrRestore(
    std::uint64_t connection_id,
    const common::net::RequestContext& context) {
    const auto binding_iter = client_bindings_.find(connection_id);
    if (binding_iter != client_bindings_.end()) {
        if (binding_iter->second.session_id == context.session_id &&
            binding_iter->second.player_id == context.player_id) {
            return {Status::kBound, {}};
        }
        return {Status::kInvalid, "connection session binding mismatch"};
    }

    const auto restored_binding = RestoreBindingFromSession(context);
    if (!restored_binding.has_value()) {
        return {Status::kInvalid, "session restore failed"};
    }

    client_bindings_[connection_id] = *restored_binding;
    return {Status::kRestored, {}};
}

void SessionBindingAuthorizer::Bind(std::uint64_t connection_id,
                                    const std::string& session_id,
                                    std::int64_t player_id) {
    client_bindings_[connection_id] = {session_id, player_id};
}

void SessionBindingAuthorizer::Unbind(std::uint64_t connection_id) {
    client_bindings_.erase(connection_id);
}

std::optional<SessionBindingAuthorizer::ClientBinding> SessionBindingAuthorizer::RestoreBindingFromSession(
    const common::net::RequestContext& context) const {
    if (context.session_id.empty() || context.player_id == 0) {
        return std::nullopt;
    }

    const auto session = session_repository_.FindById(context.session_id);
    if (!session.has_value() || session->player_id != context.player_id) {
        return std::nullopt;
    }

    return ClientBinding{session->session_id, session->player_id};
}

}  // namespace gateway
