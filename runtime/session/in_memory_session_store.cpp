#include "runtime/session/in_memory_session_store.h"

#include "runtime/foundation/security/session_token.h"

#include <chrono>
#include <stdexcept>

namespace common::session {

common::model::Session InMemorySessionStore::Create(std::int64_t account_id, std::int64_t player_id) {
    common::model::Session session;
    session.account_id = account_id;
    session.player_id = player_id;
    session.created_at_epoch_seconds = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    session.expires_at_epoch_seconds = session.created_at_epoch_seconds + 3600;

    const auto session_token = common::security::SessionToken::GenerateHex();
    if (!session_token.has_value()) {
        throw std::runtime_error("failed to generate secure session token");
    }
    session.session_id = *session_token;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto active_iter = account_sessions_.find(account_id);
    if (active_iter != account_sessions_.end()) {
        sessions_.erase(active_iter->second);
    }
    sessions_[session.session_id] = session;
    account_sessions_[account_id] = session.session_id;
    return session;
}

std::optional<common::model::Session> InMemorySessionStore::FindById(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = sessions_.find(session_id);
    if (iter == sessions_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

}  // namespace common::session
