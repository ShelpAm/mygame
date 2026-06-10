#pragma once

#include "core/math.hpp"
#include <cstdint>

using EntityId = std::uint32_t;
constexpr EntityId invalid_entity = 0;

struct Position {
    Vec2f world_pos;
    Vec2i tile_pos;
    float z_order = 0.f;
};
