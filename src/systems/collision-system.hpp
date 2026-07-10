#pragma once

#include "components/collider.hpp"
#include "core/math.hpp"
#include <flecs.h>

class NavigationSystem;

class CollisionSystem {
  public:
    void set_navigation(NavigationSystem const *nav) { navigation_ = nav; }

    // Push foot_pos out of blocked tiles overlapping the entity AABB.
    Vec2f resolve_tile_collisions(Vec2f foot_pos, Collider const &c) const;

  private:
    NavigationSystem const *navigation_ = nullptr;
};
