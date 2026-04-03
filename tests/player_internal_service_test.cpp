#include "modules/player/application/player_service.h"
#include "modules/player/infrastructure/in_memory_player_repository.h"
#include "modules/player/interfaces/grpc/player_internal_service_impl.h"

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

class FailingPlayerCacheRepository final : public game_server::player::PlayerCacheRepository {
public:
    bool Save(const common::model::PlayerState& /*player_state*/) override {
        return true;
    }

    std::optional<common::model::PlayerState> FindByPlayerId(std::int64_t /*player_id*/) const override {
        return std::nullopt;
    }

    bool Invalidate(std::int64_t /*player_id*/) override {
        return false;
    }
};

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

common::config::SimpleConfig BuildConfig() {
    common::config::SimpleConfig config;
    const std::string config_path = "player_internal_service_test.conf";
    std::ofstream output(config_path);
    output << "demo.account_id=10001\n";
    output << "demo.player_id=20001\n";
    output << "demo.player_name=hero_demo\n";
    output << "demo.level=10\n";
    output << "demo.stamina=120\n";
    output << "demo.gold=1000\n";
    output << "demo.diamond=100\n";
    output.close();
    config.LoadFromFile(config_path);
    return config;
}

}  // namespace

int main() {
    const auto config = BuildConfig();
    auto player_repository = game_server::player::InMemoryPlayerRepository::FromConfig(config);
    InMemoryPlayerCacheRepository cache_repository;
    game_server::player::PlayerService player_service(player_repository, cache_repository);
    game_server::player::PlayerInternalServiceImpl grpc_service(player_service);

    {
        grpc::ServerContext context;
        game_backend::internal::player::GetPlayerSnapshotRequest request;
        game_backend::internal::player::GetPlayerSnapshotResponse response;
        request.set_player_id(20001);
        const auto status = grpc_service.GetPlayerSnapshot(&context, &request, &response);
        if (!Expect(status.ok(), "expected grpc snapshot call to succeed")) {
            return 1;
        }
        if (!Expect(response.found(), "expected player snapshot to be found")) {
            return 1;
        }
        if (!Expect(response.level() == 10 && response.stamina() == 120, "expected snapshot payload to match")) {
            return 1;
        }
    }

    {
        grpc::ServerContext context;
        game_backend::internal::player::GetPlayerSnapshotRequest request;
        game_backend::internal::player::GetPlayerSnapshotResponse response;
        request.set_player_id(99999);
        const auto status = grpc_service.GetPlayerSnapshot(&context, &request, &response);
        if (!Expect(status.ok(), "expected missing player snapshot call to succeed")) {
            return 1;
        }
        if (!Expect(!response.found(), "expected missing player snapshot to report found=false")) {
            return 1;
        }
    }

    {
        grpc::ServerContext context;
        game_backend::internal::player::GetPlayerSnapshotRequest request;
        game_backend::internal::player::GetPlayerSnapshotResponse response;
        request.set_player_id(0);
        const auto status = grpc_service.GetPlayerSnapshot(&context, &request, &response);
        if (!Expect(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                    "expected invalid player id to map to invalid argument")) {
            return 1;
        }
    }

    {
        grpc::ServerContext context;
        game_backend::internal::player::InvalidatePlayerCacheRequest request;
        game_backend::internal::player::InvalidatePlayerCacheResponse response;
        request.set_player_id(20001);
        const auto status = grpc_service.InvalidatePlayerCache(&context, &request, &response);
        if (!Expect(status.ok() && response.success(), "expected cache invalidation to succeed")) {
            return 1;
        }
    }

    {
        auto failing_repository = game_server::player::InMemoryPlayerRepository::FromConfig(config);
        FailingPlayerCacheRepository failing_cache;
        game_server::player::PlayerService failing_service(failing_repository, failing_cache);
        game_server::player::PlayerInternalServiceImpl failing_grpc_service(failing_service);
        grpc::ServerContext context;
        game_backend::internal::player::InvalidatePlayerCacheRequest request;
        game_backend::internal::player::InvalidatePlayerCacheResponse response;
        request.set_player_id(20001);
        const auto status = failing_grpc_service.InvalidatePlayerCache(&context, &request, &response);
        if (!Expect(status.error_code() == grpc::StatusCode::INTERNAL,
                    "expected cache invalidation failures to map to internal")) {
            return 1;
        }
    }

    return 0;
}
