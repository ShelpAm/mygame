#include "world/world-state.hpp"
#include "world/map-data.hpp"
#include <cmath>
void WorldState::update(float dt)
{
    accumulator_ += dt;
    while (accumulator_ >= day_length) {
        accumulator_ -= day_length;
        day_++;
        if (day_ > 1 && (day_ - 1) % 90 == 0) {
            season_ = (season_ + 1) % 4;
        }
    }
    time_of_day_ = 6.f + (accumulator_ / day_length) * 24.f;
    if (time_of_day_ >= 24.f)
        time_of_day_ -= 24.f;
}

WorldState::LocationState const *WorldState::location(std::string const &id) const
{
    auto it = locations_.find(id);
    return it != locations_.end() ? &it->second : nullptr;
}

WorldState::LocationState *WorldState::location_mutable(std::string const &id)
{
    auto it = locations_.find(id);
    return it != locations_.end() ? &it->second : nullptr;
}

void WorldState::add_location(std::string const &id, LocationState state)
{
    locations_[id] = std::move(state);
}

// --- PlayerVisibility methods ---

void PlayerVisibility::explore_radius(Vec2f center_world, float radius_world)
{
    Vec2i min_tile =
        world_to_tile(Vec2f(center_world.x - radius_world, center_world.y - radius_world));
    Vec2i max_tile =
        world_to_tile(Vec2f(center_world.x + radius_world, center_world.y + radius_world));

    for (int y = min_tile.y; y <= max_tile.y; ++y)
        for (int x = min_tile.x; x <= max_tile.x; ++x) {
            auto delta = center_of_tile(Vec2i(x, y)) - center_world;
            if (((delta.x * delta.x) + (delta.y * delta.y)) <= radius_world * radius_world)
                explore_single(Vec2i(x, y));
        }
}

void PlayerVisibility::set_visible_arc(Vec2i center, int radius, Vec2f facing, float arc_deg)
{
    visible = compute_visible_arc(center, radius, facing, arc_deg);
}

void PlayerVisibility::set_visible_from_center(Vec2i center, int radius)
{
    visible.clear();
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            if ((dy * dy + dx * dx) <= radius * radius)
                visible.insert({center.x + dx, center.y + dy});
}
