#pragma once

#include "runtime/storage/mysql/mysql_client_pool.h"
#include "modules/player/ports/player_repository.h"

namespace game_server::player {

// MySQL-backed implementation of the player state storage boundary.
class MySqlPlayerRepository final : public PlayerRepository {
public:
    explicit MySqlPlayerRepository(common::mysql::MySqlClientPool& mysql_pool);

    [[nodiscard]] std::optional<common::model::PlayerState> LoadPlayerState(std::int64_t player_id) const override;

private:
    common::mysql::MySqlClientPool& mysql_pool_;
};

}  // namespace game_server::player
