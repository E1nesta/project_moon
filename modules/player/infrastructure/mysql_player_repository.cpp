#include "modules/player/infrastructure/mysql_player_repository.h"

#include <algorithm>
#include <sstream>

namespace game_server::player {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

int ChapterIdFromStageId(int stage_id) {
    return stage_id > 0 ? stage_id / 1000 : 0;
}

std::string EscapeString(common::mysql::MySqlClient& mysql, const std::string& value) {
    return mysql.Escape(value);
}

void SortCurrencies(std::vector<common::model::CurrencyBalance>* currencies) {
    if (currencies == nullptr) {
        return;
    }
    std::sort(currencies->begin(), currencies->end(), [](const auto& left, const auto& right) {
        return left.currency_type < right.currency_type;
    });
}

}  // namespace

MySqlPlayerRepository::MySqlPlayerRepository(common::mysql::MySqlClientPool& mysql_pool) : mysql_pool_(mysql_pool) {}

std::optional<common::model::PlayerState> MySqlPlayerRepository::LoadPlayerState(std::int64_t player_id) const {
    auto mysql = mysql_pool_.Acquire();
    std::ostringstream profile_sql;
    profile_sql << "SELECT player_id, account_id, server_id, nickname, level, energy, main_stage_id, fight_power "
                   "FROM "
                << ProfileTable(player_id) << " WHERE player_id = " << player_id << " LIMIT 1";
    const auto profile_row = mysql->QueryOne(profile_sql.str());
    if (!profile_row.has_value()) {
        return std::nullopt;
    }

    common::model::PlayerState state;
    state.profile.player_id = std::stoll(profile_row->at("player_id"));
    state.profile.account_id = std::stoll(profile_row->at("account_id"));
    state.profile.server_id = std::stoi(profile_row->at("server_id"));
    state.profile.nickname = profile_row->at("nickname");
    state.profile.player_name = state.profile.nickname;
    state.profile.level = std::stoi(profile_row->at("level"));
    state.profile.stamina = std::stoi(profile_row->at("energy"));
    state.profile.main_stage_id = std::stoi(profile_row->at("main_stage_id"));
    state.profile.main_chapter_id = ChapterIdFromStageId(state.profile.main_stage_id);
    state.profile.fight_power = std::stoll(profile_row->at("fight_power"));

    std::ostringstream currency_sql;
    currency_sql << "SELECT currency_type, amount FROM " << CurrencyTable(player_id) << " WHERE player_id = " << player_id;
    const auto currency_rows = mysql->Query(currency_sql.str());
    for (const auto& row : currency_rows) {
        common::model::CurrencyBalance currency;
        currency.currency_type = row.at("currency_type");
        currency.amount = std::stoll(row.at("amount"));
        state.currencies.push_back(currency);
        if (currency.currency_type == "gold") {
            state.profile.gold = currency.amount;
        } else if (currency.currency_type == "diamond") {
            state.profile.diamond = currency.amount;
        }
    }
    SortCurrencies(&state.currencies);

    std::ostringstream role_sql;
    role_sql << "SELECT role_id, level, star FROM " << RoleTable(player_id) << " WHERE player_id = " << player_id
             << " ORDER BY role_id ASC LIMIT 8";
    const auto role_rows = mysql->Query(role_sql.str());
    for (const auto& row : role_rows) {
        state.role_summaries.push_back({std::stoi(row.at("role_id")), std::stoi(row.at("level")), std::stoi(row.at("star"))});
    }

    return state;
}

BattleEntrySnapshotResult MySqlPlayerRepository::GetBattleEntrySnapshot(std::int64_t player_id) const {
    auto mysql = mysql_pool_.Acquire();
    std::ostringstream sql;
    sql << "SELECT level, energy FROM " << ProfileTable(player_id) << " WHERE player_id = " << player_id << " LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return {true, false, 0, 0, {}, PlayerMutationError::kNone, ""};
    }

    std::vector<common::model::PlayerRoleSummary> role_summaries;
    std::ostringstream role_sql;
    role_sql << "SELECT role_id, level, star FROM " << RoleTable(player_id) << " WHERE player_id = " << player_id
             << " ORDER BY role_id ASC LIMIT 3";
    for (const auto& role_row : mysql->Query(role_sql.str())) {
        role_summaries.push_back(
            {std::stoi(role_row.at("role_id")), std::stoi(role_row.at("level")), std::stoi(role_row.at("star"))});
    }
    return {true,
            true,
            std::stoi(row->at("level")),
            std::stoi(row->at("energy")),
            std::move(role_summaries),
            PlayerMutationError::kNone,
            ""};
}

PrepareBattleEntryResult MySqlPlayerRepository::PrepareBattleEntry(std::int64_t player_id,
                                                                  std::int64_t session_id,
                                                                  int energy_cost,
                                                                  const std::string& idempotency_key) {
    auto mysql = mysql_pool_.Acquire();
    std::string error_message;
    if (!mysql->BeginTransaction(&error_message)) {
        return {false, 0, PlayerMutationError::kStorageFailure, error_message};
    }

    PrepareBattleEntryResult result;
    bool ok = false;
    do {
        std::ostringstream check_sql;
        check_sql << "SELECT txn_id, ref_id, delta_amount, after_amount FROM " << CurrencyTxnTable(player_id)
                  << " WHERE player_id = " << player_id << " AND currency_type = 'energy' AND idempotency_key = '"
                  << EscapeString(*mysql, CurrencyIdempotencyKey(idempotency_key, "energy")) << "' LIMIT 1";
        if (const auto row = mysql->QueryOne(check_sql.str(), &error_message); row.has_value()) {
            const auto expected_delta = -energy_cost;
            if (std::stoll(row->at("txn_id")) != session_id || row->at("ref_id") != std::to_string(session_id) ||
                std::stoll(row->at("delta_amount")) != expected_delta) {
                result = {false, 0, PlayerMutationError::kBattleMismatch, "battle entry request mismatch"};
                break;
            }
            result = {true, std::stoi(row->at("after_amount")), PlayerMutationError::kNone, ""};
            ok = true;
            break;
        }

        std::ostringstream select_sql;
        select_sql << "SELECT energy FROM " << ProfileTable(player_id) << " WHERE player_id = " << player_id << " LIMIT 1";
        const auto row = mysql->QueryOne(select_sql.str(), &error_message);
        if (!row.has_value()) {
            result = {false, 0, PlayerMutationError::kPlayerNotFound, "player not found"};
            break;
        }

        const auto current_energy = std::stoi(row->at("energy"));
        if (current_energy < energy_cost) {
            result = {false, current_energy, PlayerMutationError::kStaminaNotEnough, "stamina not enough"};
            break;
        }

        std::uint64_t affected_rows = 0;
        std::ostringstream update_sql;
        update_sql << "UPDATE " << ProfileTable(player_id) << " SET energy = energy - " << energy_cost
                   << ", updated_at = CURRENT_TIMESTAMP(3) WHERE player_id = " << player_id
                   << " AND energy >= " << energy_cost;
        if (!mysql->Execute(update_sql.str(), &error_message, &affected_rows) || affected_rows != 1) {
            result = {false, 0, PlayerMutationError::kStorageFailure, error_message};
            break;
        }

        const auto remain_energy = current_energy - energy_cost;
        std::ostringstream insert_txn_sql;
        insert_txn_sql << "INSERT INTO " << CurrencyTxnTable(player_id)
                       << " (txn_id, player_id, currency_type, delta_amount, before_amount, after_amount, reason_code, "
                          "ref_type, ref_id, idempotency_key, created_at) VALUES ("
                       << session_id << ", " << player_id << ", 'energy', " << -energy_cost << ", " << current_energy << ", "
                       << remain_energy << ", 'battle_enter', 'battle_session', '" << session_id << "', '"
                       << EscapeString(*mysql, CurrencyIdempotencyKey(idempotency_key, "energy"))
                       << "', CURRENT_TIMESTAMP(3))";
        if (!mysql->Execute(insert_txn_sql.str(), &error_message)) {
            result = {false, 0, PlayerMutationError::kStorageFailure, error_message};
            break;
        }

        result = {true, remain_energy, PlayerMutationError::kNone, ""};
        ok = true;
    } while (false);

    if (!ok) {
        mysql->Rollback();
        return result;
    }
    if (!mysql->Commit(&error_message)) {
        mysql->Rollback();
        return {false, 0, PlayerMutationError::kStorageFailure, error_message};
    }
    return result;
}

CancelBattleEntryResult MySqlPlayerRepository::CancelBattleEntry(std::int64_t player_id,
                                                                std::int64_t session_id,
                                                                int energy_refund,
                                                                const std::string& idempotency_key) {
    auto mysql = mysql_pool_.Acquire();
    std::string error_message;
    if (!mysql->BeginTransaction(&error_message)) {
        return {false, PlayerMutationError::kStorageFailure, error_message};
    }

    bool ok = false;
    CancelBattleEntryResult result;
    do {
        std::ostringstream check_sql;
        check_sql << "SELECT txn_id FROM " << CurrencyTxnTable(player_id) << " WHERE player_id = " << player_id
                  << " AND currency_type = 'energy' AND idempotency_key = '"
                  << EscapeString(*mysql, CurrencyIdempotencyKey(idempotency_key, "energy")) << "' LIMIT 1";
        if (mysql->QueryOne(check_sql.str(), &error_message).has_value()) {
            result = {true, PlayerMutationError::kNone, ""};
            ok = true;
            break;
        }

        std::ostringstream update_sql;
        update_sql << "UPDATE " << ProfileTable(player_id) << " SET energy = energy + " << energy_refund
                   << ", updated_at = CURRENT_TIMESTAMP(3) WHERE player_id = " << player_id;
        std::uint64_t affected_rows = 0;
        if (!mysql->Execute(update_sql.str(), &error_message, &affected_rows) || affected_rows != 1) {
            result = {false, PlayerMutationError::kStorageFailure, error_message};
            break;
        }

        std::ostringstream balance_sql;
        balance_sql << "SELECT energy FROM " << ProfileTable(player_id) << " WHERE player_id = " << player_id << " LIMIT 1";
        const auto balance_row = mysql->QueryOne(balance_sql.str(), &error_message);
        if (!balance_row.has_value()) {
            result = {false, PlayerMutationError::kStorageFailure, "player not found after refund"};
            break;
        }
        const auto after_energy = std::stoi(balance_row->at("energy"));
        const auto before_energy = after_energy - energy_refund;

        std::ostringstream insert_sql;
        insert_sql << "INSERT INTO " << CurrencyTxnTable(player_id)
                   << " (txn_id, player_id, currency_type, delta_amount, before_amount, after_amount, reason_code, ref_type, "
                      "ref_id, idempotency_key, created_at) VALUES ("
                   << (session_id + 1) << ", " << player_id << ", 'energy', " << energy_refund << ", "
                   << before_energy << ", " << after_energy << ", "
                   << "'battle_cancel', 'battle_session', '" << session_id << "', '"
                   << EscapeString(*mysql, CurrencyIdempotencyKey(idempotency_key, "energy")) << "', CURRENT_TIMESTAMP(3))";
        if (!mysql->Execute(insert_sql.str(), &error_message)) {
            result = {false, PlayerMutationError::kStorageFailure, error_message};
            break;
        }

        result = {true, PlayerMutationError::kNone, ""};
        ok = true;
    } while (false);

    if (!ok) {
        mysql->Rollback();
        return result;
    }
    if (!mysql->Commit(&error_message)) {
        mysql->Rollback();
        return {false, PlayerMutationError::kStorageFailure, error_message};
    }
    return result;
}

ApplyRewardGrantResult MySqlPlayerRepository::ApplyRewardGrant(std::int64_t player_id,
                                                              std::int64_t grant_id,
                                                              std::int64_t session_id,
                                                              const std::vector<common::model::Reward>& rewards,
                                                              const std::string& idempotency_key) {
    auto mysql = mysql_pool_.Acquire();
    std::string error_message;
    if (!mysql->BeginTransaction(&error_message)) {
        return {false, {}, PlayerMutationError::kStorageFailure, error_message};
    }

    ApplyRewardGrantResult result;
    bool ok = false;
    do {
        for (const auto& reward : rewards) {
            std::ostringstream check_sql;
            check_sql << "SELECT txn_id, currency_type, ref_id, delta_amount, after_amount FROM "
                      << CurrencyTxnTable(player_id) << " WHERE player_id = " << player_id << " AND currency_type = '"
                      << EscapeString(*mysql, reward.reward_type)
                      << "' AND idempotency_key = '"
                      << EscapeString(*mysql, CurrencyIdempotencyKey(idempotency_key, reward.reward_type))
                      << "' LIMIT 1";
            if (const auto existing = mysql->QueryOne(check_sql.str(), &error_message); existing.has_value()) {
                const auto expected_txn_id = grant_id + (reward.reward_type == "diamond" ? 2 : 1);
                if (std::stoll(existing->at("txn_id")) != expected_txn_id ||
                    existing->at("ref_id") != std::to_string(session_id) ||
                    std::stoll(existing->at("delta_amount")) != reward.amount) {
                    result = {false, {}, PlayerMutationError::kBattleMismatch, "reward grant request mismatch"};
                    break;
                }
                result.applied_currencies.push_back({existing->at("currency_type"), std::stoll(existing->at("after_amount"))});
                continue;
            }

            std::ostringstream select_currency_sql;
            select_currency_sql << "SELECT amount FROM " << CurrencyTable(player_id) << " WHERE player_id = " << player_id
                                << " AND currency_type = '" << EscapeString(*mysql, reward.reward_type) << "' LIMIT 1";
            const auto row = mysql->QueryOne(select_currency_sql.str(), &error_message);
            const auto before_amount = row.has_value() ? std::stoll(row->at("amount")) : 0;
            const auto after_amount = before_amount + reward.amount;

            std::ostringstream upsert_sql;
            upsert_sql << "INSERT INTO " << CurrencyTable(player_id)
                       << " (player_id, currency_type, amount, version, updated_at) VALUES ("
                       << player_id << ", '" << EscapeString(*mysql, reward.reward_type) << "', " << reward.amount
                       << ", 1, CURRENT_TIMESTAMP(3)) ON DUPLICATE KEY UPDATE amount = amount + VALUES(amount), "
                          "version = version + 1, updated_at = CURRENT_TIMESTAMP(3)";
            if (!mysql->Execute(upsert_sql.str(), &error_message)) {
                result = {false, {}, PlayerMutationError::kStorageFailure, error_message};
                break;
            }

            std::ostringstream txn_sql;
            txn_sql << "INSERT INTO " << CurrencyTxnTable(player_id)
                    << " (txn_id, player_id, currency_type, delta_amount, before_amount, after_amount, reason_code, ref_type, "
                       "ref_id, idempotency_key, created_at) VALUES ("
                    << (grant_id + (reward.reward_type == "diamond" ? 2 : 1)) << ", " << player_id << ", '"
                    << EscapeString(*mysql, reward.reward_type) << "', " << reward.amount << ", " << before_amount << ", "
                    << after_amount << ", 'battle_reward', 'battle_session', '" << session_id << "', '"
                    << EscapeString(*mysql, CurrencyIdempotencyKey(idempotency_key, reward.reward_type))
                    << "', CURRENT_TIMESTAMP(3))";
            if (!mysql->Execute(txn_sql.str(), &error_message)) {
                result = {false, {}, PlayerMutationError::kStorageFailure, error_message};
                break;
            }

            result.applied_currencies.push_back({reward.reward_type, after_amount});
        }
        if (!result.error_message.empty()) {
            break;
        }
        result.success = true;
        ok = true;
    } while (false);

    if (!ok) {
        mysql->Rollback();
        return result.success ? ApplyRewardGrantResult{} : result;
    }
    if (!mysql->Commit(&error_message)) {
        mysql->Rollback();
        return {false, {}, PlayerMutationError::kStorageFailure, error_message};
    }
    return result;
}

std::string MySqlPlayerRepository::ShardSuffix(std::int64_t player_id) {
    const auto shard = static_cast<int>(player_id & 0x0F);
    std::string suffix = "00";
    suffix[0] = kHexDigits[(shard >> 4) & 0x0F];
    suffix[1] = kHexDigits[shard & 0x0F];
    return suffix;
}

std::string MySqlPlayerRepository::ProfileTable(std::int64_t player_id) {
    return "player_profile_" + ShardSuffix(player_id);
}

std::string MySqlPlayerRepository::CurrencyTable(std::int64_t player_id) {
    return "player_currency_" + ShardSuffix(player_id);
}

std::string MySqlPlayerRepository::CurrencyTxnTable(std::int64_t player_id) {
    return "currency_txn_" + ShardSuffix(player_id);
}

std::string MySqlPlayerRepository::RoleTable(std::int64_t player_id) {
    return "player_role_" + ShardSuffix(player_id);
}

std::string MySqlPlayerRepository::ItemTxnTable(std::int64_t player_id) {
    return "item_txn_" + ShardSuffix(player_id);
}

std::string MySqlPlayerRepository::PlayerOutboxTable(std::int64_t player_id) {
    return "player_outbox_" + ShardSuffix(player_id);
}

std::string MySqlPlayerRepository::CurrencyIdempotencyKey(const std::string& idempotency_key,
                                                          const std::string& currency_type) {
    return idempotency_key + ":" + currency_type;
}

}  // namespace game_server::player
