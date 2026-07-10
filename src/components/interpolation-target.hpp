#pragma once

#include "core/math.hpp"

/// Server-authoritative target position and facing for client-side
/// interpolation.  The InterpolationSystem lerps Transform toward this
/// target each frame.
struct InterpolationTarget {
    Vec2f position;
    Vec2f facing{0, -1};
};
