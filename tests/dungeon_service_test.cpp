#include "modules/dungeon/application/dungeon_service.h"
#include "modules/dungeon/infrastructure/in_memory_dungeon_config_repository.h"

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

class FakePlayerSnapshotPort final : public dungeon_server::dungeon::PlayerSnapshotPort {
public:
    explicit FakePlayerSnapshotPort(dungeon_server::dungeon::PlayerSnapshot player_snapshot) {
        players_.emplace(player_snapshot.player_id, std::move(player_snapshot));
    }

    std::optional<dungeon_server::dungeon::PlayerSnapshot> LoadPlayerSnapshot(std::int64_t player_id) const override {
        const auto iter = players_.find(player_id);
        if (iter == players_.end()) {
            return std::nullopt;
        }
        return iter->second;
    }

    bool InvalidatePlayerSnapshot(std::int64_t player_id) override {
        invalidated_players_.push_back(player_id);
        return invalidate_result_;
    }

    void SetInvalidateResult(bool invalidate_result) {
        invalidate_result_ = invalidate_result;
    }

    [[nodiscard]] bool WasInvalidated(std::int64_t player_id) const {
        return std::find(invalidated_players_.begin(), invalidated_players_.end(), player_id) != invalidated_players_.end();
    }

private:
    bool invalidate_result_ = true;
    std::unordered_map<std::int64_t, dungeon_server::dungeon::PlayerSnapshot> players_;
    std::vector<std::int64_t> invalidated_players_;
};

class FakePlayerLockRepository final : public dungeon_server::dungeon::PlayerLockRepository {
public:
    bool Acquire(std::int64_t player_id) override {
        if (!acquire_result_) {
            return false;
        }
        held_player_ids_.push_back(player_id);
        return true;
    }

    void Release(std::int64_t player_id) override {
        released_player_ids_.push_back(player_id);
    }

    void SetAcquireResult(bool acquire_result) {
        acquire_result_ = acquire_result;
    }

    [[nodiscard]] bool WasReleased(std::int64_t player_id) const {
        return std::find(released_player_ids_.begin(), released_player_ids_.end(), player_id) != released_player_ids_.end();
    }

private:
    bool acquire_result_ = true;
    std::vector<std::int64_t> held_player_ids_;
    std::vector<std::int64_t> released_player_ids_;
};

class FakeDungeonRepository final : public dungeon_server::dungeon::DungeonRepository {
public:
    std::optional<common::model::BattleContext> FindBattleById(const std::string& battle_id) const override {
        const auto iter = battles_.find(battle_id);
        if (iter == battles_.end()) {
            return std::nullopt;
        }
        return iter->second;
    }

    dungeon_server::dungeon::EnterDungeonResult EnterDungeon(const dungeon_server::dungeon::PlayerSnapshot& player_snapshot,
                                                             const dungeon_server::dungeon::DungeonConfig& dungeon_config,
                                                             const std::string& battle_id) override {
        common::model::BattleContext battle_context;
        battle_context.battle_id = battle_id;
        battle_context.player_id = player_snapshot.player_id;
        battle_context.dungeon_id = dungeon_config.dungeon_id;
        battle_context.cost_stamina = dungeon_config.cost_stamina;
        battle_context.max_star = dungeon_config.max_star;
        battle_context.settled = false;
        battles_[battle_id] = battle_context;
        return {true,
                player_snapshot.stamina - dungeon_config.cost_stamina,
                dungeon_server::dungeon::DungeonRepositoryError::kNone,
                {},
                battle_context};
    }

    dungeon_server::dungeon::SettleDungeonResult SettleDungeon(const common::model::BattleContext& battle_context,
                                                               const dungeon_server::dungeon::DungeonConfig& dungeon_config,
                                                               int star) override {
        (void)star;
        auto updated = battle_context;
        updated.settled = true;
        battles_[battle_context.battle_id] = updated;

        std::vector<common::model::Reward> rewards;
        rewards.push_back({"gold", dungeon_config.normal_gold_reward});
        rewards.push_back({"diamond", dungeon_config.first_clear_diamond_reward});
        return {true, true, dungeon_server::dungeon::DungeonRepositoryError::kNone, {}, rewards};
    }

private:
    mutable std::unordered_map<std::string, common::model::BattleContext> battles_;
};

class FakeBattleContextRepository final : public dungeon_server::dungeon::BattleContextRepository {
public:
    bool Save(const common::model::BattleContext& battle_context) override {
        battles_[battle_context.battle_id] = battle_context;
        return true;
    }

    std::optional<common::model::BattleContext> FindByBattleId(const std::string& battle_id) const override {
        const auto iter = battles_.find(battle_id);
        if (iter == battles_.end()) {
            return std::nullopt;
        }
        return iter->second;
    }

    bool Delete(const std::string& battle_id) override {
        battles_.erase(battle_id);
        return true;
    }

private:
    mutable std::unordered_map<std::string, common::model::BattleContext> battles_;
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
    dungeon_server::dungeon::PlayerSnapshot player_snapshot;
    player_snapshot.player_id = 20001;
    player_snapshot.level = 10;
    player_snapshot.stamina = 120;

    dungeon_server::dungeon::DungeonConfig config;
    config.dungeon_id = 1001;
    config.required_level = 1;
    config.cost_stamina = 10;
    config.max_star = 3;
    config.normal_gold_reward = 100;
    config.first_clear_diamond_reward = 50;

    FakePlayerSnapshotPort player_snapshot_port(player_snapshot);
    FakePlayerLockRepository lock_repository;
    dungeon_server::dungeon::InMemoryDungeonConfigRepository config_repository(config);
    FakeDungeonRepository dungeon_repository;
    FakeBattleContextRepository battle_context_repository;

    dungeon_server::dungeon::DungeonService dungeon_service(lock_repository,
                                                            player_snapshot_port,
                                                            config_repository,
                                                            dungeon_repository,
                                                            battle_context_repository);

    const auto enter_response = dungeon_service.EnterDungeon({20001, 1001});
    if (!Expect(enter_response.success, "expected enter dungeon to succeed")) {
        return 1;
    }
    if (!Expect(!enter_response.battle_id.empty(), "expected battle id to be generated")) {
        return 1;
    }
    if (!Expect(lock_repository.WasReleased(20001), "expected player lock to be released after enter")) {
        return 1;
    }

    const auto settle_response = dungeon_service.SettleDungeon({20001, enter_response.battle_id, 1001, 3, true});
    if (!Expect(settle_response.success, "expected settle dungeon to succeed")) {
        return 1;
    }
    if (!Expect(!settle_response.rewards.empty(), "expected rewards to be returned")) {
        return 1;
    }
    if (!Expect(player_snapshot_port.WasInvalidated(20001), "expected player cache invalidation")) {
        return 1;
    }

    player_snapshot_port.SetInvalidateResult(false);
    const auto enter_with_invalidation_failure = dungeon_service.EnterDungeon({20001, 1001});
    if (!Expect(enter_with_invalidation_failure.success,
                "expected enter dungeon to remain successful when invalidation fails")) {
        return 1;
    }

    lock_repository.SetAcquireResult(false);
    const auto locked_enter = dungeon_service.EnterDungeon({20001, 1001});
    if (!Expect(!locked_enter.success, "expected busy enter dungeon to fail")) {
        return 1;
    }
    if (!Expect(locked_enter.error_code == common::error::ErrorCode::kPlayerBusy,
                "expected player busy error")) {
        return 1;
    }

    return 0;
}
