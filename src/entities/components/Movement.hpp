#pragma once

#include "core/Math.hpp"

struct Movement {
    Vec2f velocity;
    Vec2f targetPos;
    float speed = 100.f;
    bool moving = false;
};
