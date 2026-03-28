#include "login_server/session/in_memory_session_repository.h"

#include <chrono>
#include <sstream>

namespace login_server::session {

common::model::Session InMemorySessionRepository::Create(std::int64_t account_id, std::int64_t player_id) {
    common::model::Session session;
    session.account_id = account_id;
    session.player_id = player_id;
    session.created_at_epoch_seconds = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    std::ostringstream stream;
    stream << "sess-" << account_id << "-" << player_id << "-" << sequence_.fetch_add(1);
    session.session_id = stream.str();

    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session.session_id] = session;
    return session;
}

std::optional<common::model::Session> InMemorySessionRepository::FindById(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = sessions_.find(session_id);
    if (iter == sessions_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

}  // namespace login_server::session

