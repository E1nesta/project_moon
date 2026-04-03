#pragma once

#include "runtime/session/session_store.h"

#include <mutex>
#include <unordered_map>

namespace common::session {

class InMemorySessionStore final : public SessionStore {
public:
    InMemorySessionStore() = default;

    common::model::Session Create(std::int64_t account_id, std::int64_t player_id) override;
    [[nodiscard]] std::optional<common::model::Session> FindById(const std::string& session_id) const override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, common::model::Session> sessions_;
    std::unordered_map<std::int64_t, std::string> account_sessions_;
};

}  // namespace common::session
