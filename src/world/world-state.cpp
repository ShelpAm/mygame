#include "world/world-state.hpp"
void WorldState::update(float dt) {
    accumulator_ += dt;
    while (accumulator_ >= day_length_) {
        accumulator_ -= day_length_;
        day_++;
        if (day_ > 1 && (day_ - 1) % 90 == 0) {
            season_ = (season_ + 1) % 4;
        }
    }
    time_of_day_ = 6.f + (accumulator_ / day_length_) * 24.f;
    if (time_of_day_ >= 24.f) time_of_day_ -= 24.f;
}

const WorldState::LocationState* WorldState::location(const std::string& id) const {
    auto it = locations_.find(id);
    return it != locations_.end() ? &it->second : nullptr;
}

WorldState::LocationState* WorldState::location_mutable(const std::string& id) {
    auto it = locations_.find(id);
    return it != locations_.end() ? &it->second : nullptr;
}

void WorldState::add_location(const std::string& id, LocationState state) {
    locations_[id] = std::move(state);
}

bool WorldState::is_tile_seen(Vec2i tile) const {
    return seen_tiles_.contains(tile);
}

void WorldState::reveal_tile(Vec2i tile) {
    seen_tiles_.insert(tile);
}

void WorldState::reveal_radius(Vec2i center, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            seen_tiles_.insert({center.x + dx, center.y + dy});
        }
    }
}
