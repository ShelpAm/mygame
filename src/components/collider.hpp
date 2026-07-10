#pragma once

#include "core/math.hpp"

// Axis-Aligned Bounding Box collider.
// min/max are relative to the entity's world_pos (foot position).
// World-space AABB: {foot_pos + min, foot_pos + max}.
struct Collider {
    Vec2f min;
    Vec2f max;
};
