#include "login_server/session/redis_session_repository.h"

#include <chrono>
#include <sstream>

namespace login_server::session {

RedisSessionRepository RedisSessionRepository::FromConfig(common::redis::RedisClientPool& redis_pool,
                                                          const common::config::SimpleConfig& config) {
    return RedisSessionRepository(redis_pool, config.GetInt("storage.session.ttl_seconds", 3600));
}

RedisSessionRepository::RedisSessionRepository(common::redis::RedisClientPool& redis_pool, int session_ttl_seconds)
    : redis_pool_(redis_pool), session_ttl_seconds_(session_ttl_seconds) {}

common::model::Session RedisSessionRepository::Create(std::int64_t account_id, std::int64_t player_id) {
    auto redis = redis_pool_.Acquire();
    common::model::Session session;
    session.account_id = account_id;
    session.player_id = player_id;
    session.created_at_epoch_seconds = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    std::ostringstream session_id;
    session_id << "sess-" << account_id << "-" << player_id << "-" << sequence_.fetch_add(1);
    session.session_id = session_id.str();

    const auto account_key = AccountSessionKey(account_id);
    if (const auto old_session = redis->Get(account_key); old_session.has_value()) {
        redis->Del(SessionKey(*old_session));
    }

    redis->HSet(SessionKey(session.session_id),
                {{"account_id", std::to_string(session.account_id)},
                 {"player_id", std::to_string(session.player_id)},
                 {"created_at_epoch_seconds", std::to_string(session.created_at_epoch_seconds)}},
                session_ttl_seconds_);
    redis->Set(account_key, session.session_id, session_ttl_seconds_);
    return session;
}

std::optional<common::model::Session> RedisSessionRepository::FindById(const std::string& session_id) const {
    auto redis = redis_pool_.Acquire();
    const auto values = redis->HGetAll(SessionKey(session_id));
    if (!values.has_value() || values->empty()) {
        return std::nullopt;
    }

    common::model::Session session;
    session.session_id = session_id;
    session.account_id = std::stoll(values->at("account_id"));
    session.player_id = std::stoll(values->at("player_id"));
    session.created_at_epoch_seconds = std::stoll(values->at("created_at_epoch_seconds"));
    return session;
}

std::string RedisSessionRepository::SessionKey(const std::string& session_id) {
    return "session:" + session_id;
}

std::string RedisSessionRepository::AccountSessionKey(std::int64_t account_id) {
    return "account:session:" + std::to_string(account_id);
}

}  // namespace login_server::session
