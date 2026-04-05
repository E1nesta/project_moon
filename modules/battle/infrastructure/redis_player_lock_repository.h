#pragma once

#include "runtime/storage/redis/redis_client_pool.h"
#include "modules/battle/ports/player_lock_repository.h"

#include <cstdint>
#include <string>

namespace battle_server::battle {

// Redis-backed implementation of the player lock boundary.
class RedisPlayerLockRepository final : public PlayerLockRepository {
public:
    explicit RedisPlayerLockRepository(common::redis::RedisClientPool& redis_pool, int ttl_seconds = 10);

    bool Acquire(std::int64_t player_id) override;
    void Release(std::int64_t player_id) override;

private:
    [[nodiscard]] std::string PlayerLockKey(std::int64_t player_id) const;

    common::redis::RedisClientPool& redis_pool_;
    int ttl_seconds_ = 10;
};

}  // namespace battle_server::battle
