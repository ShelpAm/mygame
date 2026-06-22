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

    int min_tx = static_cast<int>(std::floor((pos.x - radius) / tile_size));
    int max_tx = static_cast<int>(std::floor((pos.x + radius) / tile_size));
    int min_ty = static_cast<int>(std::floor((pos.y - radius) / tile_size));
    int max_ty = static_cast<int>(std::floor((pos.y + radius) / tile_size));

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            if (navigation_->is_walkable({tx, ty}))
                continue;

            float closest_x = std::max(tx * tile_size, std::min(resolved.x, (tx + 1) * tile_size));
            float closest_y = std::max(ty * tile_size, std::min(resolved.y, (ty + 1) * tile_size));

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
                    // Entity centre inside blocked tile — push toward nearest
                    // neighbouring walkable tile, or nearest edge as fallback
                    float best_push = std::numeric_limits<float>::max();
                    float push_x = 0.f, push_y = 0.f;
                    Vec2i const neighbors[] = {{tx + 1, ty}, {tx - 1, ty}, {tx, ty + 1}, {tx, ty - 1}};
                    for (auto n : neighbors) {
                        if (!navigation_->is_walkable(n))
                            continue;
                        float edge_x = 0.f, edge_y = 0.f;
                        if (n.x > tx)
                            edge_x = (tx + 1) * tile_size + radius;
                        else if (n.x < tx)
                            edge_x = tx * tile_size - radius;
                        if (n.y > ty)
                            edge_y = (ty + 1) * tile_size + radius;
                        else if (n.y < ty)
                            edge_y = ty * tile_size - radius;
                        float px = edge_x != 0.f ? edge_x - resolved.x : 0.f;
                        float py = edge_y != 0.f ? edge_y - resolved.y : 0.f;
                        float push = std::hypot(px, py);
                        if (push < best_push) {
                            best_push = push;
                            push_x = px;
                            push_y = py;
                        }
                    }
                    if (best_push < std::numeric_limits<float>::max()) {
                        resolved.x += push_x;
                        resolved.y += push_y;
                    }
                    else {
                        // No walkable neighbour: fall back to nearest edge
                        float to_left = resolved.x - tx * tile_size;
                        float to_right = (tx + 1) * tile_size - resolved.x;
                        float to_top = resolved.y - ty * tile_size;
                        float to_bottom = (ty + 1) * tile_size - resolved.y;
                        float min_push = std::min({to_left, to_right, to_top, to_bottom});
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
    }

    return resolved;
}
