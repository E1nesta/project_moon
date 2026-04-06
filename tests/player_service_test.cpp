#include "modules/player/infrastructure/in_memory_player_repository.h"
#include "modules/player/application/player_service.h"

#include <fstream>
#include <iostream>
#include <unordered_map>

namespace {

class InMemoryPlayerCacheRepository final : public game_server::player::PlayerCacheRepository {
public:
    void SetSaveShouldSucceed(bool value) {
        save_should_succeed_ = value;
    }

    void SetInvalidateShouldSucceed(bool value) {
        invalidate_should_succeed_ = value;
    }

    void Prime(const common::model::PlayerState& player_state) {
        cache_[player_state.profile.player_id] = player_state;
    }

    bool Save(const common::model::PlayerState& player_state) override {
        if (!save_should_succeed_) {
            return false;
        }
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
        if (!invalidate_should_succeed_) {
            return false;
        }
        cache_.erase(player_id);
        return true;
    }

private:
    std::unordered_map<std::int64_t, common::model::PlayerState> cache_;
    bool save_should_succeed_ = true;
    bool invalidate_should_succeed_ = true;
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

    auto player_repository = game_server::player::InMemoryPlayerRepository::FromConfig(config);
    InMemoryPlayerCacheRepository cache_repository;
    game_server::player::PlayerService player_service(player_repository, cache_repository);

    const auto first_load = player_service.LoadPlayer(20001);
    if (!Expect(first_load.success, "expected first load to succeed")) {
        return 1;
    }
    if (!Expect(!first_load.loaded_from_cache, "expected first load to miss cache")) {
        return 1;
    }
    if (!Expect(first_load.player_state.profile.main_stage_id == 0 &&
                    first_load.player_state.profile.main_chapter_id == 0 &&
                    first_load.player_state.stage_progress.empty(),
                "expected chapter/stage fields to be initialized in load response")) {
        return 1;
    }

    const auto second_load = player_service.LoadPlayer(20001);
    if (!Expect(second_load.success, "expected second load to succeed")) {
        return 1;
    }
    if (!Expect(second_load.loaded_from_cache, "expected second load to hit cache")) {
        return 1;
    }

    cache_repository.SetInvalidateShouldSucceed(false);

    const auto missing_player = player_service.LoadPlayer(99999);
    if (!Expect(!missing_player.success, "expected missing player load to fail")) {
        return 1;
    }
    if (!Expect(missing_player.error_code == common::error::ErrorCode::kPlayerNotFound,
                "expected player not found error code")) {
        return 1;
    }

    constexpr std::int64_t session_id = 2000110011;
    const auto spend_stamina = player_service.PrepareBattleEntry(20001, session_id, 10, "battle-enter:20001:2000110011");
    if (!Expect(spend_stamina.success, "expected spend stamina to succeed")) {
        return 1;
    }
    if (!Expect(spend_stamina.remain_energy == 110, "expected stamina to be reduced")) {
        return 1;
    }

    const auto spend_stamina_retry =
        player_service.PrepareBattleEntry(20001, session_id, 10, "battle-enter:20001:2000110011");
    if (!Expect(spend_stamina_retry.success && spend_stamina_retry.remain_energy == 110,
                "expected spend stamina retry to be idempotent")) {
        return 1;
    }

    const auto spend_stamina_mismatch =
        player_service.PrepareBattleEntry(20001, session_id, 11, "battle-enter:20001:2000110011");
    if (!Expect(!spend_stamina_mismatch.success &&
                    spend_stamina_mismatch.error_code == common::error::ErrorCode::kBattleMismatch,
                "expected spend stamina mismatch to surface battle mismatch")) {
        return 1;
    }

    const std::vector<common::model::Reward> rewards = {{"gold", 100}, {"diamond", 50}};
    const auto settlement =
        player_service.ApplyRewardGrant(20001, session_id, session_id, rewards, "battle-settle:20001:2000110011");
    if (!Expect(settlement.success, "expected reward grant to succeed")) {
        return 1;
    }
    if (!Expect(settlement.applied_currencies.size() == 2 &&
                    settlement.applied_currencies[0].currency_type == "gold" &&
                    settlement.applied_currencies[0].amount == 100 &&
                    settlement.applied_currencies[1].currency_type == "diamond" &&
                    settlement.applied_currencies[1].amount == 50,
                "expected synchronous reward grant result")) {
        return 1;
    }

    const auto settlement_retry =
        player_service.ApplyRewardGrant(20001, session_id, session_id, rewards, "battle-settle:20001:2000110011");
    if (!Expect(settlement_retry.success && settlement_retry.applied_currencies.size() == 2,
                "expected reward grant retry to be idempotent")) {
        return 1;
    }

    const auto settlement_mismatch =
        player_service.ApplyRewardGrant(20001, session_id + 1, session_id, rewards, "battle-settle:20001:2000110011");
    if (!Expect(!settlement_mismatch.success &&
                    settlement_mismatch.error_code == common::error::ErrorCode::kBattleMismatch,
                "expected reward grant mismatch to surface battle mismatch")) {
        return 1;
    }

    const auto reloaded = player_service.LoadPlayer(20001);
    if (!Expect(reloaded.success, "expected reload after reward grant to succeed")) {
        return 1;
    }
    if (!Expect(reloaded.loaded_from_cache, "expected reload after reward grant to use refreshed cache")) {
        return 1;
    }
    if (!Expect(reloaded.player_state.profile.stamina == 110 && reloaded.player_state.profile.gold == 1100 &&
                    reloaded.player_state.profile.diamond == 150,
                "expected reload to reflect synchronous stamina and reward changes")) {
        return 1;
    }

    auto fallback_player_repository = game_server::player::InMemoryPlayerRepository::FromConfig(config);
    InMemoryPlayerCacheRepository fallback_cache_repository;
    game_server::player::PlayerService fallback_player_service(fallback_player_repository, fallback_cache_repository);

    const auto fallback_first_load = fallback_player_service.LoadPlayer(20001);
    if (!Expect(fallback_first_load.success && !fallback_first_load.loaded_from_cache,
                "expected fallback service first load to succeed from storage")) {
        return 1;
    }
    fallback_cache_repository.SetSaveShouldSucceed(false);

    const auto fallback_spend =
        fallback_player_service.PrepareBattleEntry(20001, session_id + 100, 10, "battle-enter:20001:2000110111");
    if (!Expect(fallback_spend.success, "expected fallback spend stamina to succeed")) {
        return 1;
    }

    const auto fallback_reload = fallback_player_service.LoadPlayer(20001);
    if (!Expect(fallback_reload.success && !fallback_reload.loaded_from_cache,
                "expected fallback reload to miss cache after save failure")) {
        return 1;
    }
    if (!Expect(fallback_reload.player_state.profile.stamina == 110,
                "expected fallback reload to observe latest stamina from storage")) {
        return 1;
    }

    return 0;
}
