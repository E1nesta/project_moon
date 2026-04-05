#pragma once

#include "modules/battle/domain/stage_config.h"

#include <optional>

namespace battle_server::battle {

class StageConfigRepository {
public:
    virtual ~StageConfigRepository() = default;

    [[nodiscard]] virtual std::optional<StageConfig> FindByStageId(int stage_id) const = 0;
};

}  // namespace battle_server::battle
