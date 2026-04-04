#include "apps/battle_reward_worker/battle_reward_worker_app.h"

int main(int argc, char* argv[]) {
    services::dungeon::BattleRewardWorkerApp app;
    return app.Main(argc, argv);
}
