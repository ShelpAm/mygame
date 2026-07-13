#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <vector>

class ResourceManager;
struct AnimationClip;

/// Register all default animation clips for every entity kind into ResourceManager.
void register_default_clips(ResourceManager &resources);

/// Pick which animation clip should play based on game state.
/// Returns clip name (e.g. "idle", "run", "hurt", "attack", "die").
/// hurt_timer and attack_timer are passed by value (caller tracks HitFlash).
std::string determine_clip_name(float hurt_timer, float attack_timer,
                                 Vec2f velocity, bool alive);
