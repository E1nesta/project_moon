#include "modules/player/infrastructure/mysql_player_repository.h"

#include <algorithm>
#include <sstream>

namespace game_server::player {

MySqlPlayerRepository::MySqlPlayerRepository(common::mysql::MySqlClientPool& mysql_pool) : mysql_pool_(mysql_pool) {}

std::optional<common::model::PlayerState> MySqlPlayerRepository::LoadPlayerState(std::int64_t player_id) const {
    auto mysql = mysql_pool_.Acquire();
    std::ostringstream profile_sql;
    profile_sql << "SELECT p.player_id, p.account_id, p.name, p.level, "
                   "COALESCE(a.stamina, 0) AS stamina, COALESCE(a.gold, 0) AS gold, COALESCE(a.diamond, 0) AS diamond "
                   "FROM player p "
                   "LEFT JOIN player_asset a ON a.player_id = p.player_id "
                   "WHERE p.player_id = "
                << player_id << " LIMIT 1";

    const auto row = mysql->QueryOne(profile_sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    common::model::PlayerState state;
    state.profile.player_id = std::stoll(row->at("player_id"));
    state.profile.account_id = std::stoll(row->at("account_id"));
    state.profile.player_name = row->at("name");
    state.profile.level = std::stoi(row->at("level"));
    state.profile.stamina = std::stoi(row->at("stamina"));
    state.profile.gold = std::stoll(row->at("gold"));
    state.profile.diamond = std::stoll(row->at("diamond"));

    std::ostringstream progress_sql;
    progress_sql << "SELECT dungeon_id, best_star, is_first_clear FROM player_dungeon WHERE player_id = " << player_id;
    const auto progress_rows = mysql->Query(progress_sql.str());
    for (const auto& progress_row : progress_rows) {
        common::model::PlayerDungeonProgress progress;
        progress.dungeon_id = std::stoi(progress_row.at("dungeon_id"));
        progress.best_star = std::stoi(progress_row.at("best_star"));
        progress.is_first_clear = progress_row.at("is_first_clear") == "1";
        state.dungeon_progress.push_back(progress);
    }

    std::sort(state.dungeon_progress.begin(), state.dungeon_progress.end(), [](const auto& left, const auto& right) {
        return left.dungeon_id < right.dungeon_id;
    });
    return state;
}

}  // namespace game_server::player
