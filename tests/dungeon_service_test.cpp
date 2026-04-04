#include "modules/dungeon/application/dungeon_service.h"
#include "modules/dungeon/infrastructure/in_memory_dungeon_config_repository.h"
#include "modules/player/domain/player_dungeon_progress.h"

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

    dungeon_server::dungeon::SpendStaminaResponse SpendStaminaForDungeonEnter(std::int64_t player_id,
                                                                              const std::string& battle_id,
                                                                              int stamina_cost) override {
        spent_battle_ids_.push_back(battle_id);
        if (const auto operation = spend_operations_.find(battle_id); operation != spend_operations_.end()) {
            return operation->second;
        }
        auto player = players_.find(player_id);
        if (player == players_.end()) {
            return {false, common::error::ErrorCode::kPlayerNotFound, "player not found", 0};
        }
        if (!spend_stamina_success_) {
            return {false, spend_stamina_error_code_, spend_stamina_error_message_, 0};
        }
        player->second.stamina -= stamina_cost;
        const dungeon_server::dungeon::SpendStaminaResponse response{
            true, common::error::ErrorCode::kOk, "", player->second.stamina};
        spend_operations_.emplace(battle_id, response);
        return response;
    }

    dungeon_server::dungeon::ApplySettlementResponse ApplyDungeonSettlement(std::int64_t player_id,
                                                                            const std::string& battle_id,
                                                                            int dungeon_id,
                                                                            int star,
                                                                            std::int64_t normal_gold_reward,
                                                                            std::int64_t first_clear_diamond_reward) override {
        if (const auto operation = settlement_operations_.find(battle_id); operation != settlement_operations_.end()) {
            return operation->second;
        }
        auto player = players_.find(player_id);
        if (player == players_.end()) {
            return {false, common::error::ErrorCode::kPlayerNotFound, "player not found", false, {}};
        }
        if (!apply_settlement_success_) {
            return {false, apply_settlement_error_code_, apply_settlement_error_message_, false, {}};
        }

        bool first_clear = true;
        auto& progress_list = progress_by_player_[player_id];
        auto progress = std::find_if(progress_list.begin(),
                                     progress_list.end(),
                                     [dungeon_id](const common::model::PlayerDungeonProgress& item) {
                                         return item.dungeon_id == dungeon_id;
                                     });
        if (progress == progress_list.end()) {
            common::model::PlayerDungeonProgress new_progress;
            new_progress.dungeon_id = dungeon_id;
            new_progress.best_star = star;
            new_progress.is_first_clear = true;
            progress_list.push_back(new_progress);
        } else {
            first_clear = !progress->is_first_clear;
            progress->best_star = std::max(progress->best_star, star);
            progress->is_first_clear = true;
        }

        player->second.stamina = std::max(0, player->second.stamina);
        std::vector<common::model::Reward> rewards{{"gold", normal_gold_reward}};
        if (first_clear) {
            rewards.push_back({"diamond", first_clear_diamond_reward});
        }
        const dungeon_server::dungeon::ApplySettlementResponse response{
            true, common::error::ErrorCode::kOk, "", first_clear, rewards};
        settlement_operations_.emplace(battle_id, response);
        return response;
    }

    void SetInvalidateResult(bool invalidate_result) {
        invalidate_result_ = invalidate_result;
    }

    void SetSpendStaminaFailure(common::error::ErrorCode error_code, std::string error_message) {
        spend_stamina_success_ = false;
        spend_stamina_error_code_ = error_code;
        spend_stamina_error_message_ = std::move(error_message);
    }

    void SetApplySettlementFailure(common::error::ErrorCode error_code, std::string error_message) {
        apply_settlement_success_ = false;
        apply_settlement_error_code_ = error_code;
        apply_settlement_error_message_ = std::move(error_message);
    }

    [[nodiscard]] bool WasInvalidated(std::int64_t player_id) const {
        return std::find(invalidated_players_.begin(), invalidated_players_.end(), player_id) != invalidated_players_.end();
    }

    [[nodiscard]] bool WasSpentForBattle(const std::string& battle_id) const {
        return std::find(spent_battle_ids_.begin(), spent_battle_ids_.end(), battle_id) != spent_battle_ids_.end();
    }

    [[nodiscard]] int SpendCallCountForBattle(const std::string& battle_id) const {
        return static_cast<int>(std::count(spent_battle_ids_.begin(), spent_battle_ids_.end(), battle_id));
    }

private:
    bool invalidate_result_ = true;
    bool spend_stamina_success_ = true;
    bool apply_settlement_success_ = true;
    common::error::ErrorCode spend_stamina_error_code_ = common::error::ErrorCode::kOk;
    common::error::ErrorCode apply_settlement_error_code_ = common::error::ErrorCode::kOk;
    std::string spend_stamina_error_message_;
    std::string apply_settlement_error_message_;
    std::unordered_map<std::int64_t, dungeon_server::dungeon::PlayerSnapshot> players_;
    std::unordered_map<std::string, dungeon_server::dungeon::SpendStaminaResponse> spend_operations_;
    std::unordered_map<std::string, dungeon_server::dungeon::ApplySettlementResponse> settlement_operations_;
    std::unordered_map<std::int64_t, std::vector<common::model::PlayerDungeonProgress>> progress_by_player_;
    std::vector<std::int64_t> invalidated_players_;
    std::vector<std::string> spent_battle_ids_;
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

    std::optional<common::model::BattleContext> FindUnsettledBattleByPlayerId(std::int64_t player_id) const override {
        const auto iter = std::find_if(battles_.begin(), battles_.end(), [player_id](const auto& item) {
            return item.second.player_id == player_id && !item.second.settled;
        });
        if (iter == battles_.end()) {
            return std::nullopt;
        }
        return iter->second;
    }

    dungeon_server::dungeon::EnterDungeonResult PrepareEnterDungeon(std::int64_t player_id,
                                                                    int dungeon_id,
                                                                    int max_star,
                                                                    const std::string& battle_id) override {
        common::model::BattleContext battle_context;
        battle_context.battle_id = battle_id;
        battle_context.player_id = player_id;
        battle_context.dungeon_id = dungeon_id;
        battle_context.cost_stamina = 0;
        battle_context.max_star = max_star;
        battle_context.settled = false;
        battles_[battle_id] = battle_context;
        return {true, 0, dungeon_server::dungeon::DungeonRepositoryError::kNone, {}, battle_context};
    }

    bool ConfirmEnterDungeon(const std::string& battle_id,
                             int cost_stamina,
                             int remain_stamina_after,
                             std::string* error_message) override {
        const auto iter = battles_.find(battle_id);
        if (iter == battles_.end()) {
            if (error_message != nullptr) {
                *error_message = "battle not found";
            }
            return false;
        }
        iter->second.cost_stamina = cost_stamina;
        iter->second.enter_confirmed = true;
        iter->second.remain_stamina_after = remain_stamina_after;
        return true;
    }

    void CancelEnterDungeon(const std::string& battle_id) override {
        battles_.erase(battle_id);
    }

    dungeon_server::dungeon::SettleDungeonResult MarkBattleSettled(
        const std::string& battle_id,
        bool first_clear,
        const std::vector<common::model::Reward>& rewards) override {
        const auto iter = battles_.find(battle_id);
        if (iter == battles_.end()) {
            return {false, false, dungeon_server::dungeon::DungeonRepositoryError::kStorageFailure, "battle not found", {}};
        }
        iter->second.settled = true;
        iter->second.settlement_recorded = true;
        iter->second.first_clear = first_clear;
        iter->second.rewards = rewards;
        return {true, false, dungeon_server::dungeon::DungeonRepositoryError::kNone, "", {}};
    }

    void SaveBattle(common::model::BattleContext battle_context) {
        battles_[battle_context.battle_id] = std::move(battle_context);
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

    if (!Expect(player_snapshot_port.WasSpentForBattle(enter_response.battle_id),
                "expected player spend stamina rpc to be invoked")) {
        return 1;
    }
    if (!Expect(enter_response.remain_stamina == 110, "expected remain stamina after first enter")) {
        return 1;
    }

    const auto retry_enter_response = dungeon_service.EnterDungeon({20001, 1001});
    if (!Expect(retry_enter_response.success, "expected retry enter dungeon to succeed")) {
        return 1;
    }
    if (!Expect(retry_enter_response.battle_id == enter_response.battle_id,
                "expected retry enter dungeon to reuse the same battle id")) {
        return 1;
    }
    if (!Expect(retry_enter_response.remain_stamina == enter_response.remain_stamina,
                "expected retry enter dungeon to keep stamina idempotent")) {
        return 1;
    }
    if (!Expect(player_snapshot_port.SpendCallCountForBattle(enter_response.battle_id) == 1,
                "expected retry enter dungeon to replay without another player spend call")) {
        return 1;
    }

    const auto settle_response = dungeon_service.SettleDungeon({20001, enter_response.battle_id, 1001, 3, true});
    if (!Expect(settle_response.success, "expected settle dungeon to succeed")) {
        return 1;
    }
    if (!Expect(!settle_response.rewards.empty(), "expected rewards to be returned")) {
        return 1;
    }

    const auto duplicate_settle_response = dungeon_service.SettleDungeon({20001, enter_response.battle_id, 1001, 3, true});
    if (!Expect(duplicate_settle_response.success, "expected duplicate settle to replay successfully")) {
        return 1;
    }
    if (!Expect(duplicate_settle_response.first_clear == settle_response.first_clear &&
                    duplicate_settle_response.rewards.size() == settle_response.rewards.size(),
                "expected duplicate settle to replay original rewards")) {
        return 1;
    }

    player_snapshot_port.SetSpendStaminaFailure(common::error::ErrorCode::kStaminaNotEnough, "stamina not enough");
    const auto insufficient_stamina = dungeon_service.EnterDungeon({20001, 1001});
    if (!Expect(!insufficient_stamina.success &&
                    insufficient_stamina.error_code == common::error::ErrorCode::kStaminaNotEnough,
                "expected spend stamina failure to propagate")) {
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

    {
        FakePlayerSnapshotPort another_player_snapshot_port(player_snapshot);
        FakePlayerLockRepository another_lock_repository;
        dungeon_server::dungeon::InMemoryDungeonConfigRepository another_config_repository(config);
        FakeDungeonRepository another_dungeon_repository;
        FakeBattleContextRepository another_battle_context_repository;
        common::model::BattleContext foreign_battle;
        foreign_battle.battle_id = "battle-20001-2002-1";
        foreign_battle.player_id = 20001;
        foreign_battle.dungeon_id = 2002;
        foreign_battle.cost_stamina = 0;
        foreign_battle.max_star = 3;
        foreign_battle.settled = false;
        another_dungeon_repository.SaveBattle(foreign_battle);

        dungeon_server::dungeon::DungeonService another_dungeon_service(another_lock_repository,
                                                                        another_player_snapshot_port,
                                                                        another_config_repository,
                                                                        another_dungeon_repository,
                                                                        another_battle_context_repository);
        const auto cross_dungeon_enter = another_dungeon_service.EnterDungeon({20001, 1001});
        if (!Expect(!cross_dungeon_enter.success &&
                        cross_dungeon_enter.error_code == common::error::ErrorCode::kPlayerBusy,
                    "expected enter dungeon to reject unfinished battle from another dungeon")) {
            return 1;
        }
    }

    return 0;
}
