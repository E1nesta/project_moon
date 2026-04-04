#include "modules/dungeon/infrastructure/mysql_dungeon_repository.h"

#include <chrono>
#include <ctime>
#include <sstream>

namespace dungeon_server::dungeon {

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

}  // namespace

MySqlDungeonRepository::MySqlDungeonRepository(common::mysql::MySqlClientPool& mysql_pool) : mysql_pool_(&mysql_pool) {}

MySqlDungeonRepository::MySqlDungeonRepository(common::mysql::MySqlClient& mysql_client) : mysql_client_(&mysql_client) {}

std::optional<common::model::BattleContext> MySqlDungeonRepository::FindBattleById(std::int64_t session_id) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT session_id, player_id, stage_id, mode, cost_energy, remain_energy_after, seed, status "
           "FROM "
        << SessionTable() << " WHERE session_id = " << session_id << " LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    common::model::BattleContext context;
    context.session_id = std::stoll(row->at("session_id"));
    context.player_id = std::stoll(row->at("player_id"));
    context.stage_id = std::stoi(row->at("stage_id"));
    context.mode = row->at("mode");
    context.cost_energy = std::stoi(row->at("cost_energy"));
    context.remain_energy_after = std::stoi(row->at("remain_energy_after"));
    context.seed = std::stoll(row->at("seed"));
    context.settled = row->at("status") != "0";
    if (context.settled) {
        std::ostringstream grant_sql;
        grant_sql << "SELECT grant_id, grant_status, reward_json FROM " << RewardGrantTable()
                  << " WHERE session_id = " << session_id << " ORDER BY grant_id DESC LIMIT 1";
        if (const auto grant_row = mysql->QueryOne(grant_sql.str()); grant_row.has_value()) {
            context.reward_grant_id = std::stoll(grant_row->at("grant_id"));
            context.grant_status = std::stoi(grant_row->at("grant_status"));
            context.rewards = ParseRewardsFromDigest(grant_row->at("reward_json"));
        }
    }
    return context;
}

std::optional<common::model::BattleContext> MySqlDungeonRepository::FindUnsettledBattleByPlayerId(
    std::int64_t player_id) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT session_id, player_id, stage_id, mode, cost_energy, remain_energy_after, seed, status "
           "FROM "
        << SessionTable() << " WHERE player_id = " << player_id << " AND status = 0 ORDER BY start_time ASC LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    common::model::BattleContext context;
    context.session_id = std::stoll(row->at("session_id"));
    context.player_id = std::stoll(row->at("player_id"));
    context.stage_id = std::stoi(row->at("stage_id"));
    context.mode = row->at("mode");
    context.cost_energy = std::stoi(row->at("cost_energy"));
    context.remain_energy_after = std::stoi(row->at("remain_energy_after"));
    context.seed = std::stoll(row->at("seed"));
    context.settled = false;
    return context;
}

EnterBattleResult MySqlDungeonRepository::CreateBattleSession(std::int64_t session_id,
                                                             std::int64_t player_id,
                                                             int stage_id,
                                                             const std::string& mode,
                                                             int cost_energy,
                                                             int remain_energy_after,
                                                             const std::vector<common::model::PlayerRoleSummary>& role_summaries,
                                                             std::int64_t seed,
                                                             const std::string& idempotency_key,
                                                             const std::string& trace_id) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::string error_message;
    if (!mysql->BeginTransaction(&error_message)) {
        return {false, DungeonRepositoryError::kStorageFailure, error_message, {}};
    }

    EnterBattleResult result;
    bool ok = false;
    do {
        if (FindUnsettledBattleByPlayerId(player_id).has_value()) {
            result.error = DungeonRepositoryError::kUnfinishedBattleExists;
            result.error_message = "unfinished battle exists";
            break;
        }

        std::ostringstream insert_sql;
        insert_sql << "INSERT INTO " << SessionTable()
                   << " (session_id, player_id, stage_id, mode, client_version, team_hash, seed, cost_energy, "
                      "remain_energy_after, start_time, status) VALUES ("
                   << session_id << ", " << player_id << ", " << stage_id << ", '" << Escape(*mysql, mode)
                   << "', 'unknown', RPAD('', 64, '0'), " << seed << ", " << cost_energy << ", " << remain_energy_after
                   << ", CURRENT_TIMESTAMP(3), 0)";
        if (!mysql->Execute(insert_sql.str(), &error_message)) {
            result.error = DungeonRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        for (std::size_t index = 0; index < role_summaries.size(); ++index) {
            const auto& role_summary = role_summaries[index];
            std::ostringstream team_snapshot_sql;
            team_snapshot_sql << "INSERT INTO " << TeamSnapshotTable()
                              << " (session_id, slot_no, role_id, role_level, equip_digest, attr_digest) VALUES ("
                              << session_id << ", " << (index + 1) << ", " << role_summary.role_id << ", "
                              << role_summary.level << ", NULL, NULL)";
            if (!mysql->Execute(team_snapshot_sql.str(), &error_message)) {
                result.error = DungeonRepositoryError::kStorageFailure;
                result.error_message = error_message;
                break;
            }
        }
        if (result.error != DungeonRepositoryError::kNone) {
            break;
        }

        std::ostringstream outbox_sql;
        outbox_sql << "INSERT INTO battle_outbox (event_id, session_id, aggregate_type, aggregate_id, event_type, payload_json, "
                      "idempotency_key, trace_id, publish_status, retry_count, created_at, updated_at) VALUES ("
                   << session_id << ", " << session_id << ", 'battle_session', '" << session_id << "', "
                   << "'battle.session.created', JSON_OBJECT('session_id', " << session_id << ", 'player_id', " << player_id
                   << ", 'stage_id', " << stage_id << "), '" << Escape(*mysql, idempotency_key) << "', '"
                   << Escape(*mysql, trace_id) << "', 1, 0, CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3))";
        if (!mysql->Execute(outbox_sql.str(), &error_message)) {
            result.error = DungeonRepositoryError::kStorageFailure;
            result.error_message = error_message;
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
        return {false, DungeonRepositoryError::kStorageFailure, error_message, {}};
    }
    return result;
}

bool MySqlDungeonRepository::CancelBattleSession(std::int64_t session_id, std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "DELETE FROM " << SessionTable() << " WHERE session_id = " << session_id << " AND status = 0";
    return mysql->Execute(sql.str(), error_message);
}

SettleBattleResult MySqlDungeonRepository::RecordBattleSettlement(std::int64_t session_id,
                                                                 std::int64_t player_id,
                                                                 int stage_id,
                                                                 int result_code,
                                                                 int star,
                                                                 std::int64_t client_score,
                                                                 std::int64_t reward_grant_id,
                                                                 const std::vector<common::model::Reward>& rewards,
                                                                 const std::string& idempotency_key,
                                                                 const std::string& trace_id) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::string error_message;
    if (!mysql->BeginTransaction(&error_message)) {
        return {false, DungeonRepositoryError::kStorageFailure, error_message};
    }

    SettleBattleResult result;
    bool ok = false;
    do {
        std::ostringstream update_sql;
        update_sql << "UPDATE " << SessionTable()
                   << " SET status = 1, end_time = CURRENT_TIMESTAMP(3) WHERE session_id = " << session_id
                   << " AND player_id = " << player_id << " AND status = 0";
        std::uint64_t affected_rows = 0;
        if (!mysql->Execute(update_sql.str(), &error_message, &affected_rows)) {
            result.error = DungeonRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }
        if (affected_rows == 0) {
            result.error = DungeonRepositoryError::kBattleAlreadySettled;
            result.error_message = "battle already settled";
            break;
        }

        std::ostringstream result_sql;
        result_sql << "INSERT INTO " << ResultTable()
                   << " (session_id, player_id, stage_id, result_code, star, cost_time_ms, client_score, finish_time) VALUES ("
                   << session_id << ", " << player_id << ", " << stage_id << ", " << result_code << ", " << star
                   << ", 0, " << client_score << ", CURRENT_TIMESTAMP(3))";
        if (!mysql->Execute(result_sql.str(), &error_message)) {
            result.error = DungeonRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        std::ostringstream grant_sql;
        grant_sql << "INSERT INTO " << RewardGrantTable()
                  << " (grant_id, session_id, player_id, reward_json, grant_status, idempotency_key, granted_at) VALUES ("
                  << reward_grant_id << ", " << session_id << ", " << player_id << ", '"
                  << Escape(*mysql, SerializeRewardsJson(rewards)) << "', 0, '" << Escape(*mysql, idempotency_key)
                  << "', NULL)";
        if (!mysql->Execute(grant_sql.str(), &error_message)) {
            result.error = DungeonRepositoryError::kStorageFailure;
            result.error_message = error_message;
            break;
        }

        std::ostringstream outbox_sql;
        outbox_sql << "INSERT INTO battle_outbox (event_id, session_id, aggregate_type, aggregate_id, event_type, payload_json, "
                      "idempotency_key, trace_id, publish_status, retry_count, created_at, updated_at) VALUES ("
                   << reward_grant_id << ", " << session_id << ", 'reward_grant', '" << reward_grant_id << "', "
                   << "'battle.settlement.v1', JSON_OBJECT('trace_id', '" << Escape(*mysql, trace_id)
                   << "', 'session_id', " << session_id << ", 'grant_id', " << reward_grant_id << ", 'player_id', "
                   << player_id << ", 'reward_json', '" << Escape(*mysql, SerializeRewardsJson(rewards))
                   << "', 'event_id', " << reward_grant_id << "), '" << Escape(*mysql, idempotency_key) << "', '"
                   << Escape(*mysql, trace_id) << "', 0, 0, CURRENT_TIMESTAMP(3), CURRENT_TIMESTAMP(3))";
        if (!mysql->Execute(outbox_sql.str(), &error_message)) {
            result.error = DungeonRepositoryError::kStorageFailure;
            result.error_message = error_message;
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
        return {false, DungeonRepositoryError::kStorageFailure, error_message};
    }
    return result;
}

RewardGrantStatusResult MySqlDungeonRepository::GetRewardGrantStatus(std::int64_t reward_grant_id) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT reward_json, grant_status FROM " << RewardGrantTable() << " WHERE grant_id = " << reward_grant_id
        << " LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return {false, 0, {}, DungeonRepositoryError::kGrantNotFound, "reward grant not found"};
    }
    return {true, std::stoi(row->at("grant_status")), ParseRewardsFromDigest(row->at("reward_json")),
            DungeonRepositoryError::kNone, ""};
}

std::vector<BattleOutboxEvent> MySqlDungeonRepository::LoadPublishableOutboxEvents(std::size_t limit) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT bo.event_id, bo.session_id, bo.payload_json, bo.idempotency_key, bo.trace_id, bo.publish_status, "
           "bo.retry_count, rg.player_id, rg.grant_id, rg.reward_json "
           "FROM battle_outbox bo LEFT JOIN "
        << RewardGrantTable()
        << " rg ON rg.grant_id = bo.event_id WHERE bo.publish_status = 0 ORDER BY bo.created_at ASC LIMIT "
        << limit;
    const auto rows = mysql->Query(sql.str());
    std::vector<BattleOutboxEvent> events;
    for (const auto& row : rows) {
        BattleOutboxEvent event;
        event.event_id = std::stoll(row.at("event_id"));
        event.session_id = std::stoll(row.at("session_id"));
        if (const auto player_iter = row.find("player_id"); player_iter != row.end() && !player_iter->second.empty()) {
            event.player_id = std::stoll(player_iter->second);
        }
        if (const auto grant_iter = row.find("grant_id"); grant_iter != row.end() && !grant_iter->second.empty()) {
            event.reward_grant_id = std::stoll(grant_iter->second);
        }
        event.payload_json = row.at("payload_json");
        if (const auto reward_iter = row.find("reward_json"); reward_iter != row.end()) {
            event.reward_json = reward_iter->second;
        }
        event.idempotency_key = row.at("idempotency_key");
        event.trace_id = row.at("trace_id");
        event.publish_status = std::stoi(row.at("publish_status"));
        event.retry_count = std::stoi(row.at("retry_count"));
        events.push_back(std::move(event));
    }
    return events;
}

bool MySqlDungeonRepository::MarkOutboxPublished(std::int64_t event_id, std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "UPDATE battle_outbox SET publish_status = 1, published_at = CURRENT_TIMESTAMP(3), "
           "updated_at = CURRENT_TIMESTAMP(3) WHERE event_id = "
        << event_id << " AND publish_status = 0";
    return mysql->Execute(sql.str(), error_message);
}

std::vector<BattleOutboxEvent> MySqlDungeonRepository::LoadConsumableOutboxEvents(std::size_t limit) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT bo.event_id, bo.session_id, bo.payload_json, bo.idempotency_key, bo.trace_id, bo.publish_status, "
           "bo.retry_count, rg.player_id, rg.grant_id, rg.reward_json FROM battle_outbox bo JOIN "
        << RewardGrantTable()
        << " rg ON rg.grant_id = bo.event_id WHERE bo.publish_status = 1 AND rg.grant_status = 0 ORDER BY bo.created_at ASC LIMIT "
        << limit;
    const auto rows = mysql->Query(sql.str());
    std::vector<BattleOutboxEvent> events;
    for (const auto& row : rows) {
        BattleOutboxEvent event;
        event.event_id = std::stoll(row.at("event_id"));
        event.session_id = std::stoll(row.at("session_id"));
        event.player_id = std::stoll(row.at("player_id"));
        event.reward_grant_id = std::stoll(row.at("grant_id"));
        event.payload_json = row.at("payload_json");
        event.reward_json = row.at("reward_json");
        event.idempotency_key = row.at("idempotency_key");
        event.trace_id = row.at("trace_id");
        event.publish_status = std::stoi(row.at("publish_status"));
        event.retry_count = std::stoi(row.at("retry_count"));
        events.push_back(std::move(event));
    }
    return events;
}

std::optional<BattleOutboxEvent> MySqlDungeonRepository::FindOutboxEventById(std::int64_t event_id) const {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "SELECT bo.event_id, bo.session_id, bo.payload_json, bo.idempotency_key, bo.trace_id, bo.publish_status, "
           "bo.retry_count, rg.player_id, rg.grant_id, rg.reward_json FROM battle_outbox bo LEFT JOIN "
        << RewardGrantTable() << " rg ON rg.grant_id = bo.event_id WHERE bo.event_id = " << event_id << " LIMIT 1";
    const auto row = mysql->QueryOne(sql.str());
    if (!row.has_value()) {
        return std::nullopt;
    }

    BattleOutboxEvent event;
    event.event_id = std::stoll(row->at("event_id"));
    event.session_id = std::stoll(row->at("session_id"));
    if (const auto player_iter = row->find("player_id"); player_iter != row->end() && !player_iter->second.empty()) {
        event.player_id = std::stoll(player_iter->second);
    }
    if (const auto grant_iter = row->find("grant_id"); grant_iter != row->end() && !grant_iter->second.empty()) {
        event.reward_grant_id = std::stoll(grant_iter->second);
    }
    event.payload_json = row->at("payload_json");
    if (const auto reward_iter = row->find("reward_json"); reward_iter != row->end()) {
        event.reward_json = reward_iter->second;
    }
    event.idempotency_key = row->at("idempotency_key");
    event.trace_id = row->at("trace_id");
    event.publish_status = std::stoi(row->at("publish_status"));
    event.retry_count = std::stoi(row->at("retry_count"));
    return event;
}

bool MySqlDungeonRepository::ScheduleOutboxRetry(std::int64_t event_id, std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "UPDATE battle_outbox SET retry_count = retry_count + 1, next_retry_at = DATE_ADD(CURRENT_TIMESTAMP(3), "
           "INTERVAL 1 SECOND), updated_at = CURRENT_TIMESTAMP(3) WHERE event_id = "
        << event_id;
    return mysql->Execute(sql.str(), error_message);
}

bool MySqlDungeonRepository::MarkRewardGrantDone(std::int64_t reward_grant_id,
                                                 const std::vector<common::model::Reward>& rewards,
                                                 std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "UPDATE " << RewardGrantTable() << " SET grant_status = 1, reward_json = '"
        << Escape(*mysql, SerializeRewardsJson(rewards))
        << "', granted_at = CURRENT_TIMESTAMP(3) WHERE grant_id = " << reward_grant_id;
    return mysql->Execute(sql.str(), error_message);
}

bool MySqlDungeonRepository::MarkRewardGrantFailed(std::int64_t reward_grant_id, std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "UPDATE " << RewardGrantTable() << " SET grant_status = 2 WHERE grant_id = " << reward_grant_id;
    return mysql->Execute(sql.str(), error_message);
}

bool MySqlDungeonRepository::MarkOutboxConsumed(std::int64_t event_id, std::string* error_message) {
    std::optional<common::mysql::MySqlClientPool::Lease> lease;
    auto* mysql = ResolveClient(mysql_pool_, mysql_client_, &lease);
    std::ostringstream sql;
    sql << "UPDATE battle_outbox SET publish_status = 2, next_retry_at = NULL, updated_at = CURRENT_TIMESTAMP(3) "
           "WHERE event_id = "
        << event_id;
    return mysql->Execute(sql.str(), error_message);
}

std::string MySqlDungeonRepository::CurrentMonthSuffix() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buffer[7];
    std::strftime(buffer, sizeof(buffer), "%Y%m", &tm);
    return buffer;
}

std::string MySqlDungeonRepository::SessionTable() {
    return "battle_session_" + CurrentMonthSuffix();
}

std::string MySqlDungeonRepository::TeamSnapshotTable() {
    return "battle_team_snapshot_" + CurrentMonthSuffix();
}

std::string MySqlDungeonRepository::ResultTable() {
    return "battle_result_" + CurrentMonthSuffix();
}

std::string MySqlDungeonRepository::RewardGrantTable() {
    return "reward_grant_" + CurrentMonthSuffix();
}

}  // namespace dungeon_server::dungeon
