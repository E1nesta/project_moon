#include "dungeon_server/dungeon/mysql_dungeon_repository.h"

#include <sstream>

namespace dungeon_server::dungeon {

MySqlDungeonRepository::MySqlDungeonRepository(common::mysql::MySqlClient& mysql_client) : mysql_client_(mysql_client) {}

std::optional<common::model::BattleContext> MySqlDungeonRepository::FindBattleById(const std::string& battle_id) const {
    std::ostringstream sql;
    sql << "SELECT battle_id, player_id, dungeon_id, cost_stamina, status "
           "FROM dungeon_battle WHERE battle_id = '"
        << mysql_client_.Escape(battle_id) << "' LIMIT 1";
    const auto row = mysql_client_.QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    common::model::BattleContext battle_context;
    battle_context.battle_id = row->at("battle_id");
    battle_context.player_id = std::stoll(row->at("player_id"));
    battle_context.dungeon_id = std::stoi(row->at("dungeon_id"));
    battle_context.cost_stamina = std::stoi(row->at("cost_stamina"));
    battle_context.settled = row->at("status") == "1";
    return battle_context;
}

EnterDungeonResult MySqlDungeonRepository::EnterDungeon(const common::model::PlayerState& player_state,
                                                        const DungeonConfig& dungeon_config,
                                                        const std::string& battle_id) {
    EnterDungeonResult result;
    result.battle_context.battle_id = battle_id;
    result.battle_context.player_id = player_state.profile.player_id;
    result.battle_context.dungeon_id = dungeon_config.dungeon_id;
    result.battle_context.cost_stamina = dungeon_config.cost_stamina;
    result.battle_context.max_star = dungeon_config.max_star;

    std::string error_message;
    if (!mysql_client_.BeginTransaction(&error_message)) {
        result.error_message = error_message;
        return result;
    }

    bool transaction_ok = false;
    do {
        std::ostringstream pending_battle_sql;
        pending_battle_sql << "SELECT battle_id FROM dungeon_battle WHERE player_id = " << player_state.profile.player_id
                           << " AND status = 0 LIMIT 1";
        if (mysql_client_.QueryOne(pending_battle_sql.str()).has_value()) {
            result.error_message = "unfinished battle exists";
            break;
        }

        std::ostringstream update_asset_sql;
        update_asset_sql << "UPDATE player_asset SET stamina = stamina - " << dungeon_config.cost_stamina
                         << " WHERE player_id = " << player_state.profile.player_id
                         << " AND stamina >= " << dungeon_config.cost_stamina;

        std::uint64_t affected_rows = 0;
        if (!mysql_client_.Execute(update_asset_sql.str(), &error_message, &affected_rows) || affected_rows != 1) {
            result.error_message = affected_rows == 0 ? "stamina not enough" : error_message;
            break;
        }

        std::ostringstream insert_battle_sql;
        insert_battle_sql << "INSERT INTO dungeon_battle (battle_id, player_id, dungeon_id, status, cost_stamina) "
                             "VALUES ('"
                          << mysql_client_.Escape(battle_id) << "', "
                          << player_state.profile.player_id << ", "
                          << dungeon_config.dungeon_id << ", 0, "
                          << dungeon_config.cost_stamina << ")";

        if (!mysql_client_.Execute(insert_battle_sql.str(), &error_message)) {
            result.error_message = error_message;
            break;
        }

        transaction_ok = true;
    } while (false);

    if (!transaction_ok) {
        mysql_client_.Rollback();
        return result;
    }

    if (!mysql_client_.Commit(&error_message)) {
        mysql_client_.Rollback();
        result.error_message = error_message;
        return result;
    }

    result.success = true;
    result.remain_stamina = player_state.profile.stamina - dungeon_config.cost_stamina;
    return result;
}

SettleDungeonResult MySqlDungeonRepository::SettleDungeon(const common::model::BattleContext& battle_context,
                                                          const DungeonConfig& dungeon_config,
                                                          int star) {
    SettleDungeonResult result;
    std::ostringstream progress_sql;
    progress_sql << "SELECT is_first_clear FROM player_dungeon WHERE player_id = " << battle_context.player_id
                 << " AND dungeon_id = " << dungeon_config.dungeon_id << " LIMIT 1";
    const bool first_clear = !mysql_client_.QueryOne(progress_sql.str()).has_value();
    result.first_clear = first_clear;
    result.rewards.push_back({"gold", dungeon_config.normal_gold_reward});
    if (first_clear) {
        result.rewards.push_back({"diamond", dungeon_config.first_clear_diamond_reward});
    }

    std::string error_message;
    if (!mysql_client_.BeginTransaction(&error_message)) {
        result.error_message = error_message;
        return result;
    }

    bool transaction_ok = false;
    do {
        std::uint64_t affected_rows = 0;
        std::ostringstream update_battle_sql;
        update_battle_sql << "UPDATE dungeon_battle SET status = 1, finish_at = CURRENT_TIMESTAMP "
                             "WHERE battle_id = '"
                          << mysql_client_.Escape(battle_context.battle_id) << "' AND status = 0";

        if (!mysql_client_.Execute(update_battle_sql.str(), &error_message, &affected_rows) || affected_rows != 1) {
            result.error_message = affected_rows == 0 ? "battle already settled" : error_message;
            break;
        }

        std::ostringstream update_asset_sql;
        update_asset_sql << "UPDATE player_asset SET gold = gold + " << dungeon_config.normal_gold_reward;
        if (first_clear) {
            update_asset_sql << ", diamond = diamond + " << dungeon_config.first_clear_diamond_reward;
        }
        update_asset_sql << " WHERE player_id = " << battle_context.player_id;
        if (!mysql_client_.Execute(update_asset_sql.str(), &error_message)) {
            result.error_message = error_message;
            break;
        }

        std::ostringstream upsert_progress_sql;
        upsert_progress_sql
            << "INSERT INTO player_dungeon (player_id, dungeon_id, best_star, is_first_clear, last_clear_at) VALUES ("
            << battle_context.player_id << ", " << dungeon_config.dungeon_id << ", " << star << ", "
            << (first_clear ? 1 : 0)
            << ", CURRENT_TIMESTAMP) "
               "ON DUPLICATE KEY UPDATE best_star = GREATEST(best_star, VALUES(best_star)), "
               "is_first_clear = GREATEST(is_first_clear, VALUES(is_first_clear)), "
               "last_clear_at = CURRENT_TIMESTAMP";
        if (!mysql_client_.Execute(upsert_progress_sql.str(), &error_message)) {
            result.error_message = error_message;
            break;
        }

        std::ostringstream reward_log_sql;
        reward_log_sql << "INSERT INTO reward_log (player_id, battle_id, reward_type, reward_json) VALUES ("
                       << battle_context.player_id << ", '"
                       << mysql_client_.Escape(battle_context.battle_id)
                       << "', 'gold', JSON_OBJECT('amount', " << dungeon_config.normal_gold_reward << "))";
        if (!mysql_client_.Execute(reward_log_sql.str(), &error_message)) {
            result.error_message = error_message;
            break;
        }

        if (first_clear) {
            std::ostringstream first_clear_reward_log_sql;
            first_clear_reward_log_sql
                << "INSERT INTO reward_log (player_id, battle_id, reward_type, reward_json) VALUES ("
                << battle_context.player_id << ", '"
                << mysql_client_.Escape(battle_context.battle_id)
                << "', 'diamond', JSON_OBJECT('amount', " << dungeon_config.first_clear_diamond_reward << "))";
            if (!mysql_client_.Execute(first_clear_reward_log_sql.str(), &error_message)) {
                result.error_message = error_message;
                break;
            }
        }

        transaction_ok = true;
    } while (false);

    if (!transaction_ok) {
        mysql_client_.Rollback();
        return result;
    }

    if (!mysql_client_.Commit(&error_message)) {
        mysql_client_.Rollback();
        result.error_message = error_message;
        return result;
    }

    result.success = true;
    return result;
}

}  // namespace dungeon_server::dungeon
