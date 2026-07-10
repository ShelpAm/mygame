#pragma once

#include "core/math.hpp"

struct Transform {
    Vec2f world_pos;
    Vec2f facing{1, 0};

    static constexpr uint16_t kSyncWireSize = 16;
};
