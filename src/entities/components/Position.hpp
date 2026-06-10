#pragma once

#include "core/Math.hpp"
#include <cstdint>

using EntityId = std::uint32_t;
constexpr EntityId INVALID_ENTITY = 0;

struct Position {
    Vec2f worldPos;
    Vec2i tilePos;
    float zOrder = 0.f;
};
