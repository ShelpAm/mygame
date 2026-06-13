#pragma once

#include "core/math.hpp"
#include "entities/entity-manager.hpp"

struct SoldierAI {
    EntityId follow_target = 0; // Entity to follow (player or captain)
    Vec2f formation_offset;     // Offset from leader in formation
    float follow_distance = 48.f;
    float engage_range = 200.f; // Distance to auto-engage enemies
    bool in_combat = false;
};
