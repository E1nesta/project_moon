#pragma once

#include <cstdint>

namespace dungeon_server::dungeon {

class PlayerLockRepository {
public:
    virtual ~PlayerLockRepository() = default;

    virtual bool Acquire(std::int64_t player_id) = 0;
    virtual void Release(std::int64_t player_id) = 0;
};

}  // namespace dungeon_server::dungeon
