#pragma once

#include "animation/visual.hpp"
#include "core/game-types.hpp"
#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <vector>

class ResourceManager;

/// Register all default animation clips for every entity kind into ResourceManager.
void register_default_clips(ResourceManager &resources);

/// Pick which animation should play based on game state.
/// Ticks down hurt/attack timers. Returns clip name (e.g. "idle", "run").
std::string determine_clip_name(Visual &vis, Vec2f velocity, bool alive, float dt);

/// Advance frame timer, update frame_index, flip.
/// Returns current frame's texture name c_str.
char const *tick_animation(Visual &vis, Vec2f velocity, float dt);

/// Switch Visual to a named animation clip.
/// Looks up the clip from ResourceManager using composite key:
///   kind_key(entity_kind, team, role) + "_" + clip_name
/// Sets vis.clip and resets frame_index / frame_timer.
void switch_clip(Visual &vis, std::string const &clip_name,
                 uint8_t entity_kind, uint8_t team, uint8_t role,
                 ResourceManager const &resources);
