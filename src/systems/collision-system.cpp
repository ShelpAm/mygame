#include "systems/collision-system.hpp"
#include "systems/navigation-system.hpp"
#include "world/map-data.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>

Vec2f CollisionSystem::resolve_tile_collisions(Vec2f foot_pos, Collider const &c) const
{
    assert(navigation_);

    Vec2f aabb_min = foot_pos + c.min;
    Vec2f aabb_max = foot_pos + c.max;

    // Iterate to resolve cascading overlaps (pushing out of one tile may
    // cause overlap with a neighbour).
    int constexpr kMaxIters = 3;
    for (int iter = 0; iter < kMaxIters; ++iter) {
        int min_tx = static_cast<int>(std::floor(aabb_min.x / tile_size));
        int max_tx = static_cast<int>(std::floor(aabb_max.x / tile_size));
        int min_ty = static_cast<int>(std::floor(aabb_min.y / tile_size));
        int max_ty = static_cast<int>(std::floor(aabb_max.y / tile_size));

        bool pushed = false;

        for (int ty = min_ty; ty <= max_ty; ++ty) {
            for (int tx = min_tx; tx <= max_tx; ++tx) {
                if (navigation_->is_walkable({tx, ty}))
                    continue;

                float tile_left = tx * tile_size;
                float tile_right = (tx + 1) * tile_size;
                float tile_top = ty * tile_size;
                float tile_bottom = (ty + 1) * tile_size;

                // AABB-AABB overlap — compute penetration on each axis.
                float pen_left  = aabb_max.x - tile_left;      // push left  by this
                float pen_right = tile_right - aabb_min.x;     // push right by this
                float pen_up    = aabb_max.y - tile_top;       // push up    by this
                float pen_down  = tile_bottom - aabb_min.y;    // push down  by this

                // Both axes must overlap to be inside the tile.
                if (pen_left <= 0.F || pen_right <= 0.F || pen_up <= 0.F || pen_down <= 0.F)
                    continue;

                // Shortest penetration → push along that axis.
                float min_x = std::min(pen_left, pen_right);
                float min_y = std::min(pen_up, pen_down);

                if (min_x < min_y) {
                    float push = (pen_left < pen_right) ? -pen_left : pen_right;
                    aabb_min.x += push;
                    aabb_max.x += push;
                    foot_pos.x += push;
                } else {
                    float push = (pen_up < pen_down) ? -pen_up : pen_down;
                    aabb_min.y += push;
                    aabb_max.y += push;
                    foot_pos.y += push;
                }
                pushed = true;
            }
        }

        if (!pushed)
            break;
    }

    return foot_pos;
}
