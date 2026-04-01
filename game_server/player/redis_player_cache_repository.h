#pragma once

#include "common/config/simple_config.h"
#include "common/redis/redis_client.h"
#include "game_server/player/player_cache_repository.h"

namespace game_server::player {

class RedisPlayerCacheRepository final : public PlayerCacheRepository {
public:
    static RedisPlayerCacheRepository FromConfig(common::redis::RedisClient& redis_client,
                                                 const common::config::SimpleConfig& config);

    RedisPlayerCacheRepository(common::redis::RedisClient& redis_client, int ttl_seconds);

    bool Save(const common::model::PlayerState& player_state) override;
    [[nodiscard]] std::optional<common::model::PlayerState> FindByPlayerId(std::int64_t player_id) const override;
    bool Invalidate(std::int64_t player_id) override;

private:
    [[nodiscard]] static std::string CacheKey(std::int64_t player_id);
    [[nodiscard]] static std::string SerializeProgress(const common::model::PlayerState& player_state);
    [[nodiscard]] static std::vector<common::model::PlayerDungeonProgress> ParseProgress(const std::string& raw_value);

    common::redis::RedisClient& redis_client_;
    int ttl_seconds_ = 300;
};

}  // namespace game_server::player
