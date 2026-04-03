#pragma once

#include "runtime/foundation/error/error_code.h"
#include "modules/player/domain/player_state.h"
#include "modules/player/ports/player_cache_repository.h"
#include "modules/player/domain/player_profile.h"
#include "modules/player/ports/player_repository.h"

#include <cstdint>
#include <optional>
#include <string>

namespace game_server::player {

// Application result for the player load use case.
struct LoadPlayerResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    common::model::PlayerState player_state;
    bool loaded_from_cache = false;
};

struct PlayerSnapshotResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    bool found = false;
    std::int64_t player_id = 0;
    int level = 0;
    int stamina = 0;
};

struct InvalidatePlayerCacheResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
};

// Application service that coordinates session validation, storage reads and cache population.
class PlayerService {
public:
    PlayerService(PlayerRepository& player_repository,
                  PlayerCacheRepository& player_cache_repository);

    [[nodiscard]] LoadPlayerResponse LoadPlayer(std::int64_t player_id);
    [[nodiscard]] PlayerSnapshotResponse GetPlayerSnapshot(std::int64_t player_id);
    [[nodiscard]] InvalidatePlayerCacheResponse InvalidatePlayerCache(std::int64_t player_id);

private:
    [[nodiscard]] std::optional<common::model::PlayerState> LoadCachedPlayer(std::int64_t player_id) const;
    [[nodiscard]] std::optional<common::model::PlayerState> LoadPlayerFromStorage(std::int64_t player_id) const;
    [[nodiscard]] LoadPlayerResponse BuildLoadSuccess(const common::model::PlayerState& player_state,
                                                      bool loaded_from_cache) const;

    PlayerRepository& player_repository_;
    PlayerCacheRepository& player_cache_repository_;
};

}  // namespace game_server::player
