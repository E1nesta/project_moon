#pragma once

#include "common/error/error_code.h"
#include "common/model/battle_context.h"
#include "common/model/reward.h"
#include "common/redis/redis_client.h"
#include "dungeon_server/dungeon/battle_context_repository.h"
#include "dungeon_server/dungeon/dungeon_config_repository.h"
#include "dungeon_server/dungeon/mysql_dungeon_repository.h"
#include "game_server/player/player_cache_repository.h"
#include "game_server/player/player_repository.h"
#include "login_server/session/session_repository.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dungeon_server::dungeon {

struct EnterDungeonRequest {
    std::string session_id;
    std::int64_t player_id = 0;
    int dungeon_id = 0;
};

struct EnterDungeonResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    std::string battle_id;
    int remain_stamina = 0;
};

struct SettleDungeonRequest {
    std::string session_id;
    std::int64_t player_id = 0;
    std::string battle_id;
    int dungeon_id = 0;
    int star = 0;
    bool result = true;
};

struct SettleDungeonResponse {
    bool success = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    bool first_clear = false;
    std::vector<common::model::Reward> rewards;
};

class DungeonService {
public:
    DungeonService(login_server::session::SessionRepository& session_repository,
                   common::redis::RedisClient& redis_client,
                   game_server::player::PlayerRepository& player_repository,
                   game_server::player::PlayerCacheRepository& player_cache_repository,
                   DungeonConfigRepository& dungeon_config_repository,
                   MySqlDungeonRepository& dungeon_repository,
                   BattleContextRepository& battle_context_repository);

    [[nodiscard]] EnterDungeonResponse EnterDungeon(const EnterDungeonRequest& request);
    [[nodiscard]] SettleDungeonResponse SettleDungeon(const SettleDungeonRequest& request);

private:
    [[nodiscard]] std::string GenerateBattleId(std::int64_t player_id, int dungeon_id);
    [[nodiscard]] std::string PlayerLockKey(std::int64_t player_id) const;
    [[nodiscard]] bool AcquirePlayerLock(std::int64_t player_id);
    void ReleasePlayerLock(std::int64_t player_id);

    login_server::session::SessionRepository& session_repository_;
    common::redis::RedisClient& redis_client_;
    game_server::player::PlayerRepository& player_repository_;
    game_server::player::PlayerCacheRepository& player_cache_repository_;
    DungeonConfigRepository& dungeon_config_repository_;
    MySqlDungeonRepository& dungeon_repository_;
    BattleContextRepository& battle_context_repository_;
    std::uint64_t sequence_ = 1;
};

}  // namespace dungeon_server::dungeon
