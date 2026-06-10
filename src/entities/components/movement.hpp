#pragma once

#include "core/math.hpp"
struct Movement {
    Vec2f velocity;
    Vec2f target_pos;
    float speed = 100.f;
    bool moving = false;
};
