#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "runtime/session/session_store.h"

namespace common::session {

// Redis-backed session storage shared across service processes.
class RedisSessionStore final : public SessionStore {
public:
    static RedisSessionStore FromConfig(common::redis::RedisClientPool& redis_pool,
                                        const common::config::SimpleConfig& config);

    RedisSessionStore(common::redis::RedisClientPool& redis_pool, int session_ttl_seconds);

    common::model::Session Create(std::int64_t account_id, std::int64_t player_id) override;
    [[nodiscard]] std::optional<common::model::Session> FindById(const std::string& session_id) const override;

private:
    [[nodiscard]] static std::string SessionKey(const std::string& session_id);
    [[nodiscard]] static std::string AccountSessionKey(std::int64_t account_id);

    common::redis::RedisClientPool& redis_pool_;
    int session_ttl_seconds_ = 3600;
};

}  // namespace common::session
