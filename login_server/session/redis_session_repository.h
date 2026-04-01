#pragma once

#include "common/config/simple_config.h"
#include "common/redis/redis_client.h"
#include "login_server/session/session_repository.h"

#include <atomic>

namespace login_server::session {

class RedisSessionRepository final : public SessionRepository {
public:
    static RedisSessionRepository FromConfig(common::redis::RedisClient& redis_client,
                                             const common::config::SimpleConfig& config);

    RedisSessionRepository(common::redis::RedisClient& redis_client, int session_ttl_seconds);

    common::model::Session Create(std::int64_t account_id, std::int64_t player_id) override;
    [[nodiscard]] std::optional<common::model::Session> FindById(const std::string& session_id) const override;

private:
    [[nodiscard]] static std::string SessionKey(const std::string& session_id);
    [[nodiscard]] static std::string AccountSessionKey(std::int64_t account_id);

    common::redis::RedisClient& redis_client_;
    int session_ttl_seconds_ = 3600;
    mutable std::atomic<std::uint64_t> sequence_{1};
};

}  // namespace login_server::session
