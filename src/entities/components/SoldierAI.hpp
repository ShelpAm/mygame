#pragma once

#include "core/Math.hpp"
#include <cstdint>

using EntityId = std::uint32_t;

struct SoldierAI {
    EntityId followTarget = 0;   // Entity to follow (player or captain)
    Vec2f formationOffset;       // Offset from leader in formation
    float followDistance = 48.f;
    float engageRange = 200.f;   // Distance to auto-engage enemies
    bool inCombat = false;
};
