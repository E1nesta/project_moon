#pragma once

#include "common/error/error_code.h"
#include "common/model/player_state.h"
#include "login_server/session/session_repository.h"
#include "game_server/player/player_cache_repository.h"
#include "common/model/player_profile.h"
#include "game_server/player/player_repository.h"

#include <cstdint>
#include <string>

namespace game_server::player {

struct LoadPlayerResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    common::model::PlayerState player_state;
    bool loaded_from_cache = false;
};

class PlayerService {
public:
    PlayerService(login_server::session::SessionRepository& session_repository,
                  PlayerRepository& player_repository,
                  PlayerCacheRepository& player_cache_repository);

    [[nodiscard]] LoadPlayerResponse LoadPlayer(const std::string& session_id, std::int64_t player_id);

private:
    login_server::session::SessionRepository& session_repository_;
    PlayerRepository& player_repository_;
    PlayerCacheRepository& player_cache_repository_;
};

}  // namespace game_server::player
