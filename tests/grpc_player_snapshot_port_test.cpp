#include "modules/dungeon/infrastructure/grpc_player_snapshot_port.h"
#include "modules/player/application/player_service.h"
#include "modules/player/infrastructure/in_memory_player_repository.h"
#include "modules/player/interfaces/grpc/player_internal_service_impl.h"
#include "runtime/foundation/config/simple_config.h"
#include "runtime/grpc/channel_factory.h"
#include "runtime/grpc/server_runner.h"

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

common::config::SimpleConfig BuildPlayerConfig() {
    common::config::SimpleConfig config;
    const std::string config_path = "grpc_player_snapshot_port_player.conf";
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

common::config::SimpleConfig BuildGrpcClientConfig(int port) {
    common::config::SimpleConfig config;
    const std::string config_path = "grpc_player_snapshot_port_client.conf";
    std::ofstream output(config_path);
    output << "grpc.client.player_internal.host=127.0.0.1\n";
    output << "grpc.client.player_internal.port=" << port << "\n";
    output.close();
    config.LoadFromFile(config_path);
    return config;
}

}  // namespace

int main() {
    const auto player_config = BuildPlayerConfig();
    auto player_repository = game_server::player::InMemoryPlayerRepository::FromConfig(player_config);
    InMemoryPlayerCacheRepository cache_repository;
    game_server::player::PlayerService player_service(player_repository, cache_repository);
    game_server::player::PlayerInternalServiceImpl grpc_service(player_service);

    framework::grpc::ServerRunner server_runner("127.0.0.1", 0);
    std::string error_message;
    if (!server_runner.Start(&grpc_service, &error_message)) {
        std::cerr << error_message << '\n';
        return 1;
    }

    const auto client_config = BuildGrpcClientConfig(server_runner.Port());
    dungeon_server::dungeon::GrpcPlayerSnapshotPort port(
        framework::grpc::CreateInsecureChannel(client_config, "grpc.client.player_internal."));

    const auto snapshot = port.LoadPlayerSnapshot(20001);
    if (!Expect(snapshot.has_value(), "expected grpc snapshot port to load existing player")) {
        server_runner.Shutdown();
        return 1;
    }
    if (!Expect(snapshot->level == 10 && snapshot->stamina == 120, "expected grpc snapshot values to match")) {
        server_runner.Shutdown();
        return 1;
    }

    const auto missing_snapshot = port.LoadPlayerSnapshot(99999);
    if (!Expect(!missing_snapshot.has_value(), "expected missing player snapshot to return nullopt")) {
        server_runner.Shutdown();
        return 1;
    }

    if (!Expect(port.InvalidatePlayerSnapshot(20001), "expected grpc cache invalidation to succeed")) {
        server_runner.Shutdown();
        return 1;
    }

    const auto spend_stamina = port.SpendStaminaForDungeonEnter(20001, "battle-20001-1001-1", 10);
    if (!Expect(spend_stamina.success && spend_stamina.remain_stamina == 110,
                "expected grpc spend stamina mapping to succeed")) {
        server_runner.Shutdown();
        return 1;
    }

    const auto spend_stamina_failure = port.SpendStaminaForDungeonEnter(20001, "battle-20001-1001-2", 999);
    if (!Expect(!spend_stamina_failure.success &&
                    spend_stamina_failure.error_code == common::error::ErrorCode::kStaminaNotEnough,
                "expected grpc spend stamina failure mapping")) {
        server_runner.Shutdown();
        return 1;
    }

    const auto settlement = port.ApplyDungeonSettlement(20001, "battle-20001-1001-1", 1001, 3, 100, 50);
    if (!Expect(settlement.success && settlement.first_clear && settlement.rewards.size() == 2,
                "expected grpc settlement mapping to succeed")) {
        server_runner.Shutdown();
        return 1;
    }

    const auto unavailable_port_number = server_runner.Port();
    server_runner.Shutdown();

    const auto unavailable_config = BuildGrpcClientConfig(unavailable_port_number);
    dungeon_server::dungeon::GrpcPlayerSnapshotPort unavailable_port(
        framework::grpc::CreateInsecureChannel(unavailable_config, "grpc.client.player_internal."));
    if (!Expect(!unavailable_port.InvalidatePlayerSnapshot(20001),
                "expected grpc invalidation failures to map to false")) {
        return 1;
    }
    if (!Expect(!unavailable_port.SpendStaminaForDungeonEnter(20001, "battle-20001-1001-9", 10).success,
                "expected grpc spend failures to map to false")) {
        return 1;
    }

    return 0;
}
