#pragma once

#include "common/config/simple_config.h"
#include "common/redis/redis_client.h"
#include "dungeon_server/dungeon/battle_context_repository.h"

namespace dungeon_server::dungeon {

class RedisBattleContextRepository final : public BattleContextRepository {
public:
    static RedisBattleContextRepository FromConfig(common::redis::RedisClient& redis_client,
                                                   const common::config::SimpleConfig& config);

    RedisBattleContextRepository(common::redis::RedisClient& redis_client, int ttl_seconds);

    bool Save(const common::model::BattleContext& battle_context) override;
    [[nodiscard]] std::optional<common::model::BattleContext> FindByBattleId(const std::string& battle_id) const override;
    bool Delete(const std::string& battle_id) override;

private:
    [[nodiscard]] static std::string CacheKey(const std::string& battle_id);

    common::redis::RedisClient& redis_client_;
    int ttl_seconds_ = 3600;
};

}  // namespace dungeon_server::dungeon
