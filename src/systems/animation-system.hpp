#pragma once

#include <flecs.h>

class ResourceManager;

/// Advances animation frames for every entity with Animation + Sprite.
///
/// No longer registers into the flecs pipeline — call update() explicitly
/// after world_.progress() to advance frame timers and write texture_name.
class AnimationSystem {
  public:
    AnimationSystem() = default;

    /// Advance all animation frame timers. Writes the current frame's
    /// texture name into each entity's Sprite component, then resolves
    /// the sprite name through ResourceManager (sprites.yaml) to populate
    /// the actual texture key and clipping rect.
    /// @param world  flecs world with Animation + Sprite entities
    /// @param resources  ResourceManager holding sprite definitions
    /// @param delta_time  time delta in seconds
    void update(flecs::world &world, ResourceManager const &resources, float delta_time);
};
