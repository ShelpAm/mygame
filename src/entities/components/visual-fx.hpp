#pragma once

#include "core/math.hpp"
#include <SDL3/SDL.h>
#include <string>

struct FloatingText {
    std::string text;
    float lifetime = 1.f;
    float elapsed = 0.f;
    Vec2f world_pos;
    Vec2f velocity{0.f, -40.f}; // float upward
    SDL_FColor color{1.f, 1.f, 0.f, 1.f};
};

struct HitFlash {
    float duration = 0.15f;
    float remaining = 0.f;
};
