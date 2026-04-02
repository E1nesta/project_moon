#include "dungeon_server/dungeon/redis_player_lock_repository.h"

namespace dungeon_server::dungeon {

RedisPlayerLockRepository::RedisPlayerLockRepository(common::redis::RedisClient& redis_client, int ttl_seconds)
    : redis_client_(redis_client), ttl_seconds_(ttl_seconds > 0 ? ttl_seconds : 10) {}

bool RedisPlayerLockRepository::Acquire(std::int64_t player_id) {
    bool inserted = false;
    return redis_client_.SetNxWithExpire(PlayerLockKey(player_id), "1", ttl_seconds_, &inserted) && inserted;
}

void RedisPlayerLockRepository::Release(std::int64_t player_id) {
    redis_client_.Del(PlayerLockKey(player_id));
}

std::string RedisPlayerLockRepository::PlayerLockKey(std::int64_t player_id) const {
    return "player:lock:" + std::to_string(player_id);
}

}  // namespace dungeon_server::dungeon
