#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "runtime/storage/redis/redis_client_pool.h"
#include "modules/battle/ports/battle_context_repository.h"

namespace battle_server::battle {

// Redis-backed implementation of the battle context storage boundary.
class RedisBattleContextRepository final : public BattleContextRepository {
public:
    static RedisBattleContextRepository FromConfig(common::redis::RedisClientPool& redis_pool,
                                                   const common::config::SimpleConfig& config);

    RedisBattleContextRepository(common::redis::RedisClientPool& redis_pool, int ttl_seconds);

    bool Save(const common::model::BattleContext& battle_context) override;
    [[nodiscard]] std::optional<common::model::BattleContext> FindByBattleId(std::int64_t session_id) const override;
    bool Delete(std::int64_t session_id) override;

private:
    [[nodiscard]] static std::string CacheKey(std::int64_t session_id);

    common::redis::RedisClientPool& redis_pool_;
    int ttl_seconds_ = 3600;
};

}  // namespace battle_server::battle
