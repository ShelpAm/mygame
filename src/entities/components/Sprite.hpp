#pragma once

#include "core/Math.hpp"
#include <SDL3/SDL.h>
#include <string>

struct Sprite {
    std::string textureName;
    SDL_FRect srcRect{};
    Vec2f origin;
    SDL_FColor color{1.f, 1.f, 1.f, 1.f};
    float scale = 1.f;
    bool visible = true;
};
