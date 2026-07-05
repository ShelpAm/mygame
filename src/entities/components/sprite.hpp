#pragma once

#include "core/math.hpp"
#include <SDL3/SDL.h>
#include <string>

struct Sprite {
    std::string texture_name;
    Vec2f origin;
    SDL_FColor color{1.f, 1.f, 1.f, 1.f};
    float scale = 1.f;
    bool visible = true;
};
