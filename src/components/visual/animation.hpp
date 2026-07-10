#pragma once

#include "core/game-types.hpp"

// Works with Sprite. To replace Visual

struct Animation {
    AnimationClip const *clip = nullptr; // not null

    // Animation runtime state
    std::size_t frame_index = 0;
    float frame_timer = 0.F;
    bool looping = true;
};
