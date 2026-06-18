#include "systems/collision-system.hpp"
#include "systems/navigation-system.hpp"
#include "world/map-data.hpp"
#include <algorithm>
#include <cmath>

Vec2f CollisionSystem::resolve_tile_collisions(Vec2f pos, float radius)
{
    if (!navigation_)
        return pos;

    Vec2f resolved = pos;

    int min_tx = static_cast<int>((pos.x - radius) / tile_size);
    int max_tx = static_cast<int>((pos.x + radius) / tile_size);
    int min_ty = static_cast<int>((pos.y - radius) / tile_size);
    int max_ty = static_cast<int>((pos.y + radius) / tile_size);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            if (navigation_->is_walkable({tx, ty}))
                continue;

            float closest_x =
                std::max(tx * tile_size, std::min(resolved.x, (tx + 1) * tile_size));
            float closest_y =
                std::max(ty * tile_size, std::min(resolved.y, (ty + 1) * tile_size));

            float dx = resolved.x - closest_x;
            float dy = resolved.y - closest_y;
            float dist = std::hypot(dx, dy);

            if (dist < radius) {
                if (dist > 0.001f) {
                    float overlap = radius - dist;
                    resolved.x += (dx / dist) * overlap;
                    resolved.y += (dy / dist) * overlap;
                }
                else {
                    float to_left = resolved.x - tx * tile_size;
                    float to_right = (tx + 1) * tile_size - resolved.x;
                    float to_top = resolved.y - ty * tile_size;
                    float to_bottom = (ty + 1) * tile_size - resolved.y;
                    float min_push =
                        std::min({to_left, to_right, to_top, to_bottom});
                    if (min_push == to_left)
                        resolved.x = tx * tile_size - radius;
                    else if (min_push == to_right)
                        resolved.x = (tx + 1) * tile_size + radius;
                    else if (min_push == to_top)
                        resolved.y = ty * tile_size - radius;
                    else
                        resolved.y = (ty + 1) * tile_size + radius;
                }
            }
        }
    }

    return resolved;
}
