#pragma once

#include "common/redis/redis_client_pool.h"
#include "dungeon_server/dungeon/player_lock_repository.h"

#include <cstdint>
#include <string>

namespace dungeon_server::dungeon {

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

}  // namespace dungeon_server::dungeon
