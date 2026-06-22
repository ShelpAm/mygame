#pragma once

#include "core/math.hpp"
#include <flecs.h>

class NavigationSystem;

class CollisionSystem {
  public:
    void set_navigation(NavigationSystem const *nav) { navigation_ = nav; }

    // Push position out of blocked tiles overlapping the entity circle.
    Vec2f resolve_tile_collisions(Vec2f pos, float radius);

  private:
    NavigationSystem const *navigation_ = nullptr;
};
