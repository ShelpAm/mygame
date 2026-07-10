#pragma once

#include <flecs.h>

class ResourceManager;

/// Evaluates entity state (velocity, combat stats, hit flash) each frame and
/// switches the active Animation clip accordingly (idle / run / hurt / attack / die).
///
/// Must be called *before* AnimationSystem::update() in the same frame so the
/// new clip is picked up by the frame-advancement pass.
class AnimationControllerSystem {
  public:
    AnimationControllerSystem() = default;

    /// Query all entities with Animation + Sprite + Movement + CombatStats + KindTag
    /// and determine which clip should play. Swaps clip on the Animation component
    /// and sets Sprite::flip based on movement direction.
    /// @param world     flecs world with animated entities
    /// @param resources ResourceManager holding the registered clips
    void update(flecs::world &world, ResourceManager const &resources, float);
};
