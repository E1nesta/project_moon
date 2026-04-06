#include "modules/battle/infrastructure/mysql_battle_repository.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace battle_server::battle {

namespace {

common::mysql::MySqlClient* ResolveClient(common::mysql::MySqlClientPool* pool,
                                          common::mysql::MySqlClient* client,
                                          std::optional<common::mysql::MySqlClientPool::Lease>* lease) {
    if (pool != nullptr) {
        *lease = pool->Acquire();
        return &((**lease).operator*());
    }
    return client;
}

std::string Escape(common::mysql::MySqlClient& mysql, const std::string& value) {
    return mysql.Escape(value);
}

std::string SerializeRewardsJson(const std::vector<common::model::Reward>& rewards) {
    std::ostringstream output;
    output << '[';
    bool first = true;
    for (const auto& reward : rewards) {
        if (!first) {
            output << ',';
        }
        output << "{\"reward_type\":\"" << reward.reward_type << "\",\"amount\":" << reward.amount << '}';
        first = false;
    }
    output << ']';
    return output.str();
}

std::vector<common::model::Reward> ParseRewardsFromDigest(const std::string& raw) {
    std::vector<common::model::Reward> rewards;
    std::size_t cursor = 0;
    while (true) {
        const auto type_key = raw.find("\"reward_type\"", cursor);
        if (type_key == std::string::npos) {
            break;
        }
        const auto type_value_begin = raw.find('"', raw.find(':', type_key) + 1);
        if (type_value_begin == std::string::npos) {
            break;
        }
        const auto type_value_end = raw.find('"', type_value_begin + 1);
        if (type_value_end == std::string::npos) {
            break;
        }

        const auto amount_key = raw.find("\"amount\"", type_value_end);
        if (amount_key == std::string::npos) {
            break;
        }
        const auto amount_begin = raw.find_first_of("-0123456789", raw.find(':', amount_key) + 1);
        if (amount_begin == std::string::npos) {
            break;
        }
        const auto amount_end = raw.find_first_not_of("-0123456789", amount_begin);
        rewards.push_back(
            {raw.substr(type_value_begin + 1, type_value_end - type_value_begin - 1),
             std::stoll(raw.substr(amount_begin, amount_end - amount_begin))});
        cursor = amount_end == std::string::npos ? raw.size() : amount_end;
    }
    return rewards;
}

bool IsDuplicateInsert(const std::string& error_message) {
    return error_message.find("Duplicate entry") != std::string::npos;
}

constexpr std::int64_t kCustomEpochMs = 1704067200000LL;

std::string FormatMonthSuffix(int year, int month) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << year << std::setw(2) << month;
    return output.str();
}

std::string MonthSuffixFromGeneratedId(std::int64_t id) {
    const auto generated_ms =
        static_cast<std::int64_t>((static_cast<std::uint64_t>(id) >> 22U) + static_cast<std::uint64_t>(kCustomEpochMs));
    const std::time_t tt = generated_ms / 1000;
    std::tm tm{};
    gmtime_r(&tt, &tm);
    return FormatMonthSuffix(tm.tm_year + 1900, tm.tm_mon + 1);
}

common::model::BattleContext BuildBattleContextFromRow(std::int64_t session_id,
                                                       const std::unordered_map<std::string, std::string>& row) {
    common::model::BattleContext context;
    context.session_id = session_id;
    context.player_id = std::stoll(row.at("player_id"));
    context.stage_id = std::stoi(row.at("stage_id"));
    context.mode = row.at("mode");
    context.cost_energy = std::stoi(row.at("cost_energy"));
    context.remain_energy_after = std::stoi(row.at("remain_energy_after"));
    context.seed = std::stoll(row.at("seed"));
    context.settled = row.at("status") != "0";
    return context;
}

common::model::BattleContext BuildActiveBattleContextFromRow(const std::unordered_map<std::string, std::string>& row) {
    common::model::BattleContext context;
    context.session_id = std::stoll(row.at("session_id"));
    context.player_id = std::stoll(row.at("player_id"));
    context.stage_id = std::stoi(row.at("stage_id"));
    context.mode = row.at("mode");
    context.cost_energy = std::stoi(row.at("cost_energy"));
    context.remain_energy_after = std::stoi(row.at("remain_energy_after"));
    context.seed = std::stoll(row.at("seed"));
    context.settled = false;
    return context;
}

BattleEntryOperation BuildBattleEntryOperationFromRow(const std::unordered_map<std::string, std::string>& row) {
    BattleEntryOperation operation;
    operation.player_id = std::stoll(row.at("player_id"));
    operation.stage_id = std::stoi(row.at("stage_id"));
    operation.mode = row.at("mode");
    operation.session_id = std::stoll(row.at("session_id"));
    operation.seed = std::stoll(row.at("seed"));
    operation.remain_energy_after = std::stoi(row.at("remain_energy_after"));
    operation.status = static_cast<BattleEntryOperationStatus>(std::stoi(row.at("status")));
    operation.error_code = static_cast<common::error::ErrorCode>(std::stoi(row.at("error_code")));
    operation.error_message = row.at("error_message");
    return operation;
}

}  // namespace

MySqlBattleRepository::MySqlBattleRepository(common::mysql::MySqlClientPool& mysql_pool) : mysql_pool_(&mysql_pool) {}

MySqlBattleRepository::MySqlBattleRepository(common::mysql::MySqlClient& mysql_client) : mysql_client_(&mysql_client) {}

std::optional<common::model::BattleContext> MySqlBattleRepository::FindBattleById(std::int64_t session_id) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    const auto month_suffix = MonthSuffixFromGeneratedId(session_id);
    std::ostringstream sql;
    sql << "SELECT session_id, player_id, stage_id, mode, cost_energy, remain_energy_after, seed, status "
           "FROM "
        << SessionTable(month_suffix) << " WHERE session_id = " << session_id << " LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    auto context = BuildBattleContextFromRow(session_id, *row);
    if (context.settled) {
        std::ostringstream grant_sql;
        grant_sql << "SELECT grant_id, grant_status, reward_json FROM " << RewardGrantTable(month_suffix)
                  << " WHERE session_id = " << session_id << " ORDER BY grant_id DESC LIMIT 1";
        if (const auto grant_row = mysql->QueryOne(grant_sql.str()); grant_row.has_value()) {
            context.reward_grant_id = std::stoll(grant_row->at("grant_id"));
            context.grant_status = std::stoi(grant_row->at("grant_status"));
            context.rewards = ParseRewardsFromDigest(grant_row->at("reward_json"));
        }
    }
    return context;
}

std::optional<BattleEntryOperation> MySqlBattleRepository::FindBattleEntryOperationByIdempotencyKey(
    const std::string& idempotency_key) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT player_id, stage_id, mode, session_id, seed, remain_energy_after, status, error_code, error_message FROM "
        << EnterOperationTable() << " WHERE idempotency_key = '" << Escape(*mysql, idempotency_key) << "' LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }
    return BuildBattleEntryOperationFromRow(*row);
}

bool MySqlBattleRepository::CreateBattleEntryOperation(std::int64_t player_id,
                                                       int stage_id,
                                                       const std::string& mode,
                                                       std::int64_t session_id,
                                                       std::int64_t seed,
                                                       const std::string& idempotency_key,
                                                       std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::string local_error_message;
    std::ostringstream sql;
    sql << "INSERT INTO " << EnterOperationTable()
        << " (idempotency_key, player_id, stage_id, mode, session_id, seed, remain_energy_after, status, error_code, "
           "error_message, created_at, updated_at) "
           "VALUES ('"
        << Escape(*mysql, idempotency_key) << "', " << player_id << ", " << stage_id << ", '" << Escape(*mysql, mode) << "', "
        << session_id << ", " << seed << ", 0, 0, 0, '', CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3))";
    if (!mysql->Execute(sql.str(), &local_error_message)) {
        if (error_message != nullptr) {
            *error_message = local_error_message;
        }
        return false;
    }
    return true;
}

bool MySqlBattleRepository::MarkBattleEntryOperationRolledBack(const std::string& idempotency_key,
                                                               std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::string local_error_message;
    std::uint64_t affected_rows = 0;
    std::ostringstream sql;
    sql << "UPDATE " << EnterOperationTable()
        << " SET status = 2, updated_at = CURRENT_TIMESTAMP(3) WHERE idempotency_key = '"
        << Escape(*mysql, idempotency_key) << "' AND status = 0";
    if (!mysql->Execute(sql.str(), &local_error_message, &affected_rows)) {
        if (error_message != nullptr) {
            *error_message = local_error_message;
        }
        return false;
    }
    if (affected_rows == 1) {
        return true;
    }

    if (const auto operation = FindBattleEntryOperationByIdempotencyKey(idempotency_key); operation.has_value()) {
        if (operation->status == BattleEntryOperationStatus::kRolledBack) {
            return true;
        }
        if (error_message != nullptr) {
            *error_message = "battle entry operation is not in preparing state";
        }
        return false;
    }

    if (error_message != nullptr) {
        *error_message = "battle entry operation not found";
    }
    return false;
}

bool MySqlBattleRepository::MarkBattleEntryOperationFailed(const std::string& idempotency_key,
                                                           common::error::ErrorCode error_code,
                                                           const std::string& error_message,
                                                           std::string* storage_error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::string local_error_message;
    std::uint64_t affected_rows = 0;
    std::ostringstream sql;
    sql << "UPDATE " << EnterOperationTable()
        << " SET status = 4, error_code = " << static_cast<int>(error_code)
        << ", error_message = '" << Escape(*mysql, error_message) << "', updated_at = CURRENT_TIMESTAMP(3)"
        << " WHERE idempotency_key = '" << Escape(*mysql, idempotency_key) << "' AND status = 0";
    if (!mysql->Execute(sql.str(), &local_error_message, &affected_rows)) {
        if (storage_error_message != nullptr) {
            *storage_error_message = local_error_message;
        }
        return false;
    }
    if (affected_rows == 1) {
        return true;
    }

    if (const auto operation = FindBattleEntryOperationByIdempotencyKey(idempotency_key); operation.has_value()) {
        if (operation->status == BattleEntryOperationStatus::kFailed &&
            operation->error_code == error_code &&
            operation->error_message == error_message) {
            return true;
        }
        if (storage_error_message != nullptr) {
            *storage_error_message = "battle entry operation is not in preparing state";
        }
        return false;
    }

    if (storage_error_message != nullptr) {
        *storage_error_message = "battle entry operation not found";
    }
    return false;
}

std::optional<common::model::BattleContext> MySqlBattleRepository::FindUnsettledBattleByPlayerId(
    std::int64_t player_id) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT session_id, player_id, stage_id, mode, cost_energy, remain_energy_after, seed "
           "FROM "
        << ActiveSessionTable() << " WHERE player_id = " << player_id << " LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }
    return BuildActiveBattleContextFromRow(*row);
}

std::optional<common::model::BattleContext> MySqlBattleRepository::FindUnsettledBattleByIdempotencyKey(
    const std::string& idempotency_key) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT session_id, player_id, stage_id, mode, cost_energy, remain_energy_after, seed "
           "FROM "
        << ActiveSessionTable() << " WHERE idempotency_key = '" << Escape(*mysql, idempotency_key) << "' LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }
    return BuildActiveBattleContextFromRow(*row);
}

EnterBattleResult MySqlBattleRepository::CreateBattleSession(std::int64_t session_id,
                                                             std::int64_t player_id,
                                                             int stage_id,
                                                             const std::string& mode,
                                                             int cost_energy,
                                                             int remain_energy_after,
                                                             const std::vector<BattleRoleSummary>& role_summaries,
                                                             std::int64_t seed,
                                                             const std::string& idempotency_key,
                                                             const std::string& trace_id) {
    (void)trace_id;
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    if (const auto replay = FindUnsettledBattleByIdempotencyKey(idempotency_key); replay.has_value()) {
        EnterBattleResult result;
        result.success = true;
        result.battle_context = *replay;
        return result;
    }
    std::string error_message;
    if (!mysql->BeginTransaction(&error_message)) {
        return {false, BattleRepositoryError::kStorageFailure, error_message, {}};
    }

    EnterBattleResult result;
    bool ok = false;
    do {
        const auto month_suffix = MonthSuffixFromGeneratedId(session_id);
        std::ostringstream insert_sql;
        insert_sql << "INSERT INTO " << SessionTable(month_suffix)
                   << " (session_id, player_id, stage_id, mode, client_version, team_hash, seed, cost_energy, "
                      "remain_energy_after, active_player_id, start_time, status) VALUES ("
                   << session_id << ", " << player_id << ", " << stage_id << ", '" << Escape(*mysql, mode)
                   << "', 'unknown', RPAD('', 64, '0'), " << seed << ", " << cost_energy << ", " << remain_energy_after
                   << ", " << player_id << ", CURRENT_TIMESTAMP(3), 0)";
        if (!mysql->Execute(insert_sql.str(), &error_message)) {
            if (IsDuplicateInsert(error_message) && FindUnsettledBattleByPlayerId(player_id).has_value()) {
                result.error = BattleRepositoryError::kUnfinishedBattleExists;
                result.error_message = "unfinished battle exists";
                break;
            }
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        std::ostringstream active_sql;
        active_sql << "INSERT INTO " << ActiveSessionTable()
                   << " (player_id, session_id, stage_id, mode, cost_energy, remain_energy_after, seed, "
                      "idempotency_key, created_at) VALUES ("
                   << player_id << ", " << session_id << ", " << stage_id << ", '" << Escape(*mysql, mode) << "', "
                   << cost_energy << ", " << remain_energy_after << ", " << seed << ", '"
                   << Escape(*mysql, idempotency_key) << "', CURRENT_TIMESTAMP(3))";
        if (!mysql->Execute(active_sql.str(), &error_message)) {
            if (IsDuplicateInsert(error_message)) {
                result.error = BattleRepositoryError::kUnfinishedBattleExists;
                result.error_message = "unfinished battle exists";
                break;
            }
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        for (std::size_t index = 0; index < role_summaries.size(); ++index) {
            const auto& role_summary = role_summaries[index];
            std::ostringstream team_snapshot_sql;
            team_snapshot_sql << "INSERT INTO " << TeamSnapshotTable(month_suffix)
                              << " (session_id, slot_no, role_id, role_level, equip_digest, attr_digest) VALUES ("
                              << session_id << ", " << (index + 1) << ", " << role_summary.role_id << ", "
                              << role_summary.level << ", NULL, NULL)";
            if (!mysql->Execute(team_snapshot_sql.str(), &error_message)) {
                result.error = BattleRepositoryError::kStorageFailure;
                result.error_message = error_message;
                break;
            }
        }
        if (result.error != BattleRepositoryError::kNone) {
            break;
        }

        std::ostringstream update_operation_sql;
        update_operation_sql << "UPDATE " << EnterOperationTable()
                             << " SET remain_energy_after = " << remain_energy_after
                             << ", status = 1, updated_at = CURRENT_TIMESTAMP(3) WHERE idempotency_key = '"
                             << Escape(*mysql, idempotency_key) << "' AND status = 0";
        std::uint64_t operation_rows = 0;
        if (!mysql->Execute(update_operation_sql.str(), &error_message, &operation_rows) || operation_rows != 1) {
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message = operation_rows == 0 ? "battle entry operation update affected no rows" : error_message;
            break;
        }

        result.success = true;
        result.battle_context.session_id = session_id;
        result.battle_context.player_id = player_id;
        result.battle_context.stage_id = stage_id;
        result.battle_context.mode = mode;
        result.battle_context.cost_energy = cost_energy;
        result.battle_context.remain_energy_after = remain_energy_after;
        result.battle_context.seed = seed;
        ok = true;
    } while (false);

    if (!ok) {
        mysql->Rollback();
        return result;
    }
    if (!mysql->Commit(&error_message)) {
        mysql->Rollback();
        return {false, BattleRepositoryError::kStorageFailure, error_message, {}};
    }
    return result;
}

bool MySqlBattleRepository::CancelBattleSession(std::int64_t session_id, std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::string local_error_message;
    if (!mysql->BeginTransaction(&local_error_message)) {
        if (error_message != nullptr) {
            *error_message = local_error_message;
        }
        return false;
    }

    const auto month_suffix = MonthSuffixFromGeneratedId(session_id);
    std::ostringstream delete_session_sql;
    delete_session_sql << "DELETE FROM " << SessionTable(month_suffix) << " WHERE session_id = " << session_id
                       << " AND status = 0";
    if (!mysql->Execute(delete_session_sql.str(), &local_error_message)) {
        mysql->Rollback();
        if (error_message != nullptr) {
            *error_message = local_error_message;
        }
        return false;
    }

    std::ostringstream delete_active_sql;
    delete_active_sql << "DELETE FROM " << ActiveSessionTable() << " WHERE session_id = " << session_id;
    if (!mysql->Execute(delete_active_sql.str(), &local_error_message)) {
        mysql->Rollback();
        if (error_message != nullptr) {
            *error_message = local_error_message;
        }
        return false;
    }

    if (!mysql->Commit(&local_error_message)) {
        mysql->Rollback();
        if (error_message != nullptr) {
            *error_message = local_error_message;
        }
        return false;
    }
    return true;
}

SettleBattleResult MySqlBattleRepository::RecordBattleSettlement(std::int64_t session_id,
                                                                 std::int64_t player_id,
                                                                 int stage_id,
                                                                 int result_code,
                                                                 int star,
                                                                 std::int64_t client_score,
                                                                 std::int64_t reward_grant_id,
                                                                 const std::vector<common::model::Reward>& rewards,
                                                                 const std::string& idempotency_key) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::string error_message;
    if (!mysql->BeginTransaction(&error_message)) {
        return {false, BattleRepositoryError::kStorageFailure, error_message};
    }

    SettleBattleResult result;
    bool ok = false;
    do {
        const auto session_month_suffix = MonthSuffixFromGeneratedId(session_id);
        const auto reward_month_suffix = MonthSuffixFromGeneratedId(reward_grant_id);
        std::ostringstream update_sql;
        update_sql << "UPDATE " << SessionTable(session_month_suffix)
                   << " SET status = 1, active_player_id = NULL, end_time = CURRENT_TIMESTAMP(3) WHERE session_id = "
                   << session_id
                   << " AND player_id = " << player_id << " AND status = 0";
        std::uint64_t affected_rows = 0;
        if (!mysql->Execute(update_sql.str(), &error_message, &affected_rows)) {
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }
        if (affected_rows == 0) {
            result.error = BattleRepositoryError::kBattleAlreadySettled;
            result.error_message = "battle already settled";
            break;
        }

        std::ostringstream result_sql;
        result_sql << "INSERT INTO " << ResultTable(session_month_suffix)
                   << " (session_id, player_id, stage_id, result_code, star, cost_time_ms, client_score, finish_time) VALUES ("
                   << session_id << ", " << player_id << ", " << stage_id << ", " << result_code << ", " << star
                   << ", 0, " << client_score << ", CURRENT_TIMESTAMP(3))";
        if (!mysql->Execute(result_sql.str(), &error_message)) {
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        std::ostringstream grant_sql;
        grant_sql << "INSERT INTO " << RewardGrantTable(reward_month_suffix)
                  << " (grant_id, session_id, player_id, reward_json, grant_status, idempotency_key, granted_at) VALUES ("
                  << reward_grant_id << ", " << session_id << ", " << player_id << ", '"
                  << Escape(*mysql, SerializeRewardsJson(rewards)) << "', 0, '" << Escape(*mysql, idempotency_key)
                  << "', NULL)";
        if (!mysql->Execute(grant_sql.str(), &error_message)) {
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        std::ostringstream delete_active_sql;
        delete_active_sql << "DELETE FROM " << ActiveSessionTable() << " WHERE session_id = " << session_id;
        if (!mysql->Execute(delete_active_sql.str(), &error_message)) {
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        std::ostringstream complete_operation_sql;
        complete_operation_sql << "UPDATE " << EnterOperationTable()
                               << " SET status = 3, updated_at = CURRENT_TIMESTAMP(3) WHERE session_id = " << session_id
                               << " AND status = 1";
        std::uint64_t operation_rows = 0;
        if (!mysql->Execute(complete_operation_sql.str(), &error_message, &operation_rows) || operation_rows != 1) {
            result.error = BattleRepositoryError::kStorageFailure;
            result.error_message =
                operation_rows == 0 ? "battle entry operation completion affected no rows" : error_message;
            break;
        }

        result.success = true;
        ok = true;
    } while (false);

    if (!ok) {
        mysql->Rollback();
        return result;
    }
    if (!mysql->Commit(&error_message)) {
        mysql->Rollback();
        return {false, BattleRepositoryError::kStorageFailure, error_message};
    }
    return result;
}

SettleBattleResult MySqlBattleRepository::MarkRewardGrantGranted(std::int64_t reward_grant_id) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    const auto month_suffix = MonthSuffixFromGeneratedId(reward_grant_id);

    std::ostringstream sql;
    sql << "UPDATE " << RewardGrantTable(month_suffix)
        << " SET grant_status = 1, granted_at = CURRENT_TIMESTAMP(3) WHERE grant_id = " << reward_grant_id
        << " AND grant_status = 0";

    std::string error_message;
    std::uint64_t affected_rows = 0;
    if (!mysql->Execute(sql.str(), &error_message, &affected_rows)) {
        return {false, BattleRepositoryError::kStorageFailure, error_message};
    }

    if (affected_rows == 0) {
        std::ostringstream lookup_sql;
        lookup_sql << "SELECT grant_status FROM " << RewardGrantTable(month_suffix) << " WHERE grant_id = " << reward_grant_id
                   << " LIMIT 1";
        const auto row = mysql->QueryOne(lookup_sql.str());
        if (!row.has_value()) {
            return {false, BattleRepositoryError::kGrantNotFound, "reward grant not found"};
        }
        if (row->at("grant_status") == "1") {
            return {true, BattleRepositoryError::kNone, ""};
        }
        return {false, BattleRepositoryError::kStorageFailure, "reward grant update affected no rows"};
    }

    return {true, BattleRepositoryError::kNone, ""};
}

RewardGrantStatusResult MySqlBattleRepository::GetRewardGrantStatus(std::int64_t player_id,
                                                                    std::int64_t reward_grant_id) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    const auto month_suffix = MonthSuffixFromGeneratedId(reward_grant_id);
    std::ostringstream sql;
    sql << "SELECT reward_json, grant_status FROM " << RewardGrantTable(month_suffix) << " WHERE grant_id = " << reward_grant_id
        << " AND player_id = " << player_id << " LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return {false, 0, {}, BattleRepositoryError::kGrantNotFound, "reward grant not found"};
    }
    return {true, std::stoi(row->at("grant_status")), ParseRewardsFromDigest(row->at("reward_json")),
            BattleRepositoryError::kNone, ""};
}

std::string MySqlBattleRepository::EnterOperationTable() {
    return "battle_enter_operation";
}

std::string MySqlBattleRepository::ActiveSessionTable() {
    return "battle_active_session";
}

std::string MySqlBattleRepository::SessionTable(const std::string& month_suffix) {
    return "battle_session_" + month_suffix;
}

std::string MySqlBattleRepository::TeamSnapshotTable(const std::string& month_suffix) {
    return "battle_team_snapshot_" + month_suffix;
}

std::string MySqlBattleRepository::ResultTable(const std::string& month_suffix) {
    return "battle_result_" + month_suffix;
}

std::string MySqlBattleRepository::RewardGrantTable(const std::string& month_suffix) {
    return "reward_grant_" + month_suffix;
}

}  // namespace battle_server::battle
