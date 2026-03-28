#pragma once

#include "login_server/session/session_repository.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace login_server::session {

class InMemorySessionRepository final : public SessionRepository {
public:
    InMemorySessionRepository() = default;

    common::model::Session Create(std::int64_t account_id, std::int64_t player_id) override;
    [[nodiscard]] std::optional<common::model::Session> FindById(const std::string& session_id) const override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, common::model::Session> sessions_;
    std::atomic<std::uint64_t> sequence_{1};
};

}  // namespace login_server::session

