#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include <cassert>
#include <SDL3/SDL.h>
#include <vector>

/// The visual presentation of an entity.
/// Always has a clip (set by switch_clip). Texture comes from clip->frame_names[frame_index].
struct Visual {
    AnimationClip const *clip = nullptr;

    // Animation runtime state
    std::size_t frame_index = 0;
    float frame_timer = 0.F;
    std::vector<float> frame_durations; // populated by switch_clip
    bool flip = false;

    // State machine (ticked by determine_clip_name)
    float hurt_timer = 0.F;
    float attack_timer = 0.F;

    // Per-entity visual properties (shared across all frames)
    Vec2f origin;
    SDL_FColor color{1.f, 1.f, 1.f, 1.f};
    float scale = 1.f;

    std::string const &current_frame_name() const
    {
        assert(clip != nullptr);
        return clip->frame_names[frame_index];
    }
};
