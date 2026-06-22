#pragma once

#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct AnimationClip {
    std::string name;
    std::vector<std::string> frame_names;
    float frame_duration = 0.1f;
    bool loop;
    bool faces_right;
};

struct AnimationState {
    AnimationClip const *clip = nullptr;
    int frame_index = 0;
    float frame_timer = 0.f;
    bool flip = false;        // true = face left, mirror horizontally
    float hurt_timer = 0.f;   // >0 → playing hurt animation
    float attack_timer = 0.f; // >0 → playing attack animation
};

// Returns the set of animation clips for a given entity kind + team.
std::vector<AnimationClip> const &animation_clips_for_kind(uint8_t entity_kind, uint8_t team);

// Find a clip by name within a clip vector.
AnimationClip const *find_clip(std::vector<AnimationClip> const &clips, std::string const &name);

// Pick which clip should play based on game state.
// Ticks down hurt/attack timers.
AnimationClip const *determine_clip(std::vector<AnimationClip> const &clips, AnimationState &state,
                                    Vec2f velocity, bool alive, float dt);

// Switch to a new clip (resets frame state). No-op if already on this clip.
void switch_clip(AnimationState &state, AnimationClip const *clip);

// Advance frame timer, return current frame texture name.
// Also updates flip based on horizontal velocity.
char const *tick_animation(AnimationState &state, Vec2f velocity, float dt);
