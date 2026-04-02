#pragma once

#include "common/redis/redis_client.h"
#include "dungeon_server/dungeon/player_lock_repository.h"

#include <cstdint>
#include <string>

namespace dungeon_server::dungeon {

class RedisPlayerLockRepository final : public PlayerLockRepository {
public:
    explicit RedisPlayerLockRepository(common::redis::RedisClient& redis_client, int ttl_seconds = 10);

    bool Acquire(std::int64_t player_id) override;
    void Release(std::int64_t player_id) override;

private:
    [[nodiscard]] std::string PlayerLockKey(std::int64_t player_id) const;

    common::redis::RedisClient& redis_client_;
    int ttl_seconds_ = 10;
};

}  // namespace dungeon_server::dungeon
