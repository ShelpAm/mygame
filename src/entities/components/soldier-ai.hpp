#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"

struct SoldierAI {
    EntityId follow_target = 0; // Entity to follow (player or captain)
    Vec2f formation_offset;     // Offset from leader in formation
    float follow_distance = 48.f;
    float engage_range = 200.f; // Distance to auto-engage enemies
    bool in_combat = false;
};
