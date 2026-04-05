#pragma once

#include <cstdint>

namespace battle_server::battle {

// Storage boundary for per-player concurrency control.
class PlayerLockRepository {
public:
    virtual ~PlayerLockRepository() = default;

    virtual bool Acquire(std::int64_t player_id) = 0;
    virtual void Release(std::int64_t player_id) = 0;
};

}  // namespace battle_server::battle
