#pragma once

#include "common/model/session.h"

#include <cstdint>
#include <optional>
#include <string>

namespace login_server::session {

// Storage boundary for session creation and lookup.
class SessionRepository {
public:
    virtual ~SessionRepository() = default;

    virtual common::model::Session Create(std::int64_t account_id, std::int64_t player_id) = 0;
    [[nodiscard]] virtual std::optional<common::model::Session> FindById(const std::string& session_id) const = 0;
};

}  // namespace login_server::session
