#pragma once

#include "core/math.hpp"
#include <string>

// Works with Animation. To replace Visual
struct Sprite {
    std::string name;
    std::string texture_name{"undefined-texture"};
    Vec2f offset{0, 0}; // Normalized clip offset (0~1) within the texture.
    Vec2f size{1, 1};   // Normalized clip size (0~1).  offset + size = clipping rect.
                        // Default (1,1) = full texture (no crop).
    Vec2f origin{0, 0}; // Normalized origin (0~1) within the clipping rect.
                        // Applied after clipping.
    float scale = 1.F;
    bool flip = false;
};
