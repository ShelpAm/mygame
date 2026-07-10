#pragma once

#include "core/math.hpp"

struct Movement {
    float max_speed{};
    Vec2f velocity;

    // velocity(8) = 8 bytes
    static constexpr uint16_t kSyncWireSize = 8;
};
