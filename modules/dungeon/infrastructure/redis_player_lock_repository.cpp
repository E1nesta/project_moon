#include "modules/dungeon/infrastructure/redis_player_lock_repository.h"

namespace dungeon_server::dungeon {

RedisPlayerLockRepository::RedisPlayerLockRepository(common::redis::RedisClientPool& redis_pool, int ttl_seconds)
    : redis_pool_(redis_pool), ttl_seconds_(ttl_seconds > 0 ? ttl_seconds : 10) {}

bool RedisPlayerLockRepository::Acquire(std::int64_t player_id) {
    auto redis = redis_pool_.Acquire();
    bool inserted = false;
    return redis->SetNxWithExpire(PlayerLockKey(player_id), "1", ttl_seconds_, &inserted) && inserted;
}

void RedisPlayerLockRepository::Release(std::int64_t player_id) {
    auto redis = redis_pool_.Acquire();
    redis->Del(PlayerLockKey(player_id));
}

std::string RedisPlayerLockRepository::PlayerLockKey(std::int64_t player_id) const {
    return "player:lock:" + std::to_string(player_id);
}

}  // namespace dungeon_server::dungeon
