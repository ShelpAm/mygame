#include "world/WorldState.hpp"

void WorldState::update(float dt) {
    m_accumulator += dt;
    while (m_accumulator >= m_dayLength) {
        m_accumulator -= m_dayLength;
        m_day++;
        if (m_day > 1 && (m_day - 1) % 90 == 0) {
            m_season = (m_season + 1) % 4;
        }
    }
    m_timeOfDay = 6.f + (m_accumulator / m_dayLength) * 24.f;
    if (m_timeOfDay >= 24.f) m_timeOfDay -= 24.f;
}

const WorldState::LocationState* WorldState::location(const std::string& id) const {
    auto it = m_locations.find(id);
    return it != m_locations.end() ? &it->second : nullptr;
}

WorldState::LocationState* WorldState::locationMutable(const std::string& id) {
    auto it = m_locations.find(id);
    return it != m_locations.end() ? &it->second : nullptr;
}

void WorldState::addLocation(const std::string& id, LocationState state) {
    m_locations[id] = std::move(state);
}

bool WorldState::isTileSeen(Vec2i tile) const {
    return m_seenTiles.contains(tile);
}

void WorldState::revealTile(Vec2i tile) {
    m_seenTiles.insert(tile);
}

void WorldState::revealRadius(Vec2i center, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            m_seenTiles.insert({center.x + dx, center.y + dy});
        }
    }
}
