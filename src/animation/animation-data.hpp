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

/// Look up the animation clip for a given entity kind / team / role / clip_name.
/// subtype is used for building_type (inn/market/etc.) or other sub-categories.
/// Returns nullptr if no clip is registered.
AnimationClip const *get_clip(std::string const &clip_name,
                               uint8_t entity_kind, uint8_t team, uint8_t role,
                               ResourceManager const &resources,
                               uint8_t subtype = 0);
