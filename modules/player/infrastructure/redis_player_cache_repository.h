#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "modules/player/ports/player_cache_repository.h"

namespace game_server::player {

// Redis-backed implementation of the player snapshot cache boundary.
class RedisPlayerCacheRepository final : public PlayerCacheRepository {
public:
    static RedisPlayerCacheRepository FromConfig(common::redis::RedisClientPool& redis_pool,
                                                 const common::config::SimpleConfig& config);

    RedisPlayerCacheRepository(common::redis::RedisClientPool& redis_pool, int ttl_seconds);

    bool Save(const common::model::PlayerState& player_state) override;
    [[nodiscard]] std::optional<common::model::PlayerState> FindByPlayerId(std::int64_t player_id) const override;
    bool Invalidate(std::int64_t player_id) override;

private:
    [[nodiscard]] static std::string CacheKey(std::int64_t player_id);
    [[nodiscard]] static std::string SerializeProgress(const common::model::PlayerState& player_state);
    [[nodiscard]] static std::vector<common::model::PlayerDungeonProgress> ParseProgress(const std::string& raw_value);

    common::redis::RedisClientPool& redis_pool_;
    int ttl_seconds_ = 300;
};

}  // namespace game_server::player
