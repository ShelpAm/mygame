#pragma once

#include "core/Math.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class WorldState {
public:
    void update(float dt);

    struct LocationState {
        std::string name;
        std::string regionId;
        bool playerHasVisited = false;
        int lastVisitDay = -1;
        std::string controllingFactionId;
        int population = 0;
    };

    const LocationState* location(const std::string& id) const;
    LocationState* locationMutable(const std::string& id);
    void addLocation(const std::string& id, LocationState state);

    // Tile visibility
    bool isTileSeen(Vec2i tile) const;
    void revealTile(Vec2i tile);
    void revealRadius(Vec2i center, int radius);

    int day() const { return m_day; }
    int season() const { return m_season; }
    float timeOfDay() const { return m_timeOfDay; }
    const std::unordered_set<Vec2i>& seenTiles() const { return m_seenTiles; }

    void setDay(int d) { m_day = d; }
    void setSeason(int s) { m_season = s % 4; }

private:
    std::unordered_map<std::string, LocationState> m_locations;
    std::unordered_set<Vec2i> m_seenTiles;

    int m_day = 1;
    int m_season = 0;
    float m_timeOfDay = 6.f;
    float m_dayLength = 24.f;
    float m_accumulator = 0.f;
};
