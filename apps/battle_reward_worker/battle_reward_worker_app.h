#pragma once

#include "modules/dungeon/infrastructure/grpc_player_snapshot_port.h"
#include "modules/dungeon/infrastructure/mysql_dungeon_repository.h"
#include "runtime/foundation/config/simple_config.h"
#include "runtime/mq/rocketmq_client.h"
#include "runtime/storage/mysql/mysql_client_pool.h"

#include <memory>

namespace services::dungeon {

class BattleRewardWorkerApp {
public:
    BattleRewardWorkerApp();

    int Main(int argc, char* argv[]);

private:
    bool BuildDependencies(std::string* error_message);
    void PublishPendingEvents();
    void ConsumeRewardMessages();
    bool ProcessRewardDelivery(const common::mq::RocketMqDelivery& delivery, int max_retry_count);
    int RunLoop();
    void Shutdown();

    common::config::SimpleConfig config_;
    std::unique_ptr<common::mysql::MySqlClientPool> battle_mysql_pool_;
    std::unique_ptr<dungeon_server::dungeon::MySqlDungeonRepository> battle_repository_;
    std::unique_ptr<dungeon_server::dungeon::PlayerSnapshotPort> player_snapshot_port_;
    std::unique_ptr<common::mq::RocketMqProducer> producer_;
    std::unique_ptr<common::mq::RocketMqConsumer> consumer_;
};

}  // namespace services::dungeon
