#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "modules/battle/ports/stage_config_repository.h"

namespace battle_server::battle {

class InMemoryStageConfigRepository final : public StageConfigRepository {
public:
    static InMemoryStageConfigRepository FromConfig(const common::config::SimpleConfig& config);

    explicit InMemoryStageConfigRepository(StageConfig stage_config);

    [[nodiscard]] std::optional<StageConfig> FindByStageId(int stage_id) const override;

private:
    StageConfig stage_config_;
};

}  // namespace battle_server::battle
