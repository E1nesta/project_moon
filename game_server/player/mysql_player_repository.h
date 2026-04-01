#pragma once

#include "common/mysql/mysql_client.h"
#include "game_server/player/player_repository.h"

namespace game_server::player {

class MySqlPlayerRepository final : public PlayerRepository {
public:
    explicit MySqlPlayerRepository(common::mysql::MySqlClient& mysql_client);

    [[nodiscard]] std::optional<common::model::PlayerState> LoadPlayerState(std::int64_t player_id) const override;

private:
    common::mysql::MySqlClient& mysql_client_;
};

}  // namespace game_server::player
