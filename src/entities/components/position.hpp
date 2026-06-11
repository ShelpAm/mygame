#pragma once

#include "core/math.hpp"

struct Position {
    Vec2f world_pos;
    Vec2i tile_pos;
    float z_order = 0.f;
};
