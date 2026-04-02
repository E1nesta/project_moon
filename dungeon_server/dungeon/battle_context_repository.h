#pragma once

#include "common/model/battle_context.h"

#include <optional>
#include <string>

namespace dungeon_server::dungeon {

// Storage boundary for transient battle context persistence.
class BattleContextRepository {
public:
    virtual ~BattleContextRepository() = default;

    virtual bool Save(const common::model::BattleContext& battle_context) = 0;
    [[nodiscard]] virtual std::optional<common::model::BattleContext> FindByBattleId(const std::string& battle_id) const = 0;
    virtual bool Delete(const std::string& battle_id) = 0;
};

}  // namespace dungeon_server::dungeon
