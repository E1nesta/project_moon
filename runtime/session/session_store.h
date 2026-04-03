#pragma once

#include "runtime/session/session.h"

#include <cstdint>
#include <optional>
#include <string>

namespace common::session {

// Shared read-only boundary for validating and restoring sessions across services.
class SessionReader {
public:
    virtual ~SessionReader() = default;

    [[nodiscard]] virtual std::optional<common::model::Session> FindById(const std::string& session_id) const = 0;
};

// Login-domain write boundary used to create externally visible sessions.
class SessionStore : public SessionReader {
public:
    ~SessionStore() override = default;

    virtual common::model::Session Create(std::int64_t account_id, std::int64_t player_id) = 0;
};

}  // namespace common::session
