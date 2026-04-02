#include "game_server/player/in_memory_player_repository.h"
#include "game_server/player/player_service.h"
#include "login_server/session/in_memory_session_repository.h"

#include <fstream>
#include <iostream>
#include <unordered_map>

namespace {

class InMemoryPlayerCacheRepository final : public game_server::player::PlayerCacheRepository {
public:
    bool Save(const common::model::PlayerState& player_state) override {
        cache_[player_state.profile.player_id] = player_state;
        return true;
    }

    std::optional<common::model::PlayerState> FindByPlayerId(std::int64_t player_id) const override {
        const auto iter = cache_.find(player_id);
        if (iter == cache_.end()) {
            return std::nullopt;
        }
        return iter->second;
    }

    bool Invalidate(std::int64_t player_id) override {
        cache_.erase(player_id);
        return true;
    }

private:
    std::unordered_map<std::int64_t, common::model::PlayerState> cache_;
};

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

}  // namespace

int main() {
    common::config::SimpleConfig config;
    const std::string config_path = "player_service_test.conf";
    {
        std::ofstream output(config_path);
        output << "demo.account_id=10001\n";
        output << "demo.player_id=20001\n";
        output << "demo.player_name=hero_demo\n";
        output << "demo.level=10\n";
        output << "demo.stamina=120\n";
        output << "demo.gold=1000\n";
        output << "demo.diamond=100\n";
    }

    if (!config.LoadFromFile(config_path)) {
        std::cerr << "failed to load player service test config\n";
        return 1;
    }

    login_server::session::InMemorySessionRepository session_repository;
    auto session = session_repository.Create(10001, 20001);
    auto player_repository = game_server::player::InMemoryPlayerRepository::FromConfig(config);
    InMemoryPlayerCacheRepository cache_repository;
    game_server::player::PlayerService player_service(session_repository, player_repository, cache_repository);

    const auto first_load = player_service.LoadPlayer(session.session_id, 20001);
    if (!Expect(first_load.success, "expected first load to succeed")) {
        return 1;
    }
    if (!Expect(!first_load.loaded_from_cache, "expected first load to miss cache")) {
        return 1;
    }

    const auto second_load = player_service.LoadPlayer(session.session_id, 20001);
    if (!Expect(second_load.success, "expected second load to succeed")) {
        return 1;
    }
    if (!Expect(second_load.loaded_from_cache, "expected second load to hit cache")) {
        return 1;
    }

    const auto invalid_session = player_service.LoadPlayer("invalid-session", 20001);
    if (!Expect(!invalid_session.success, "expected invalid session load to fail")) {
        return 1;
    }
    if (!Expect(invalid_session.error_code == common::error::ErrorCode::kSessionInvalid,
                "expected invalid session error code")) {
        return 1;
    }

    return 0;
}
