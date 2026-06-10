#pragma once

#include "core/Math.hpp"
#include <vector>
#include <unordered_set>

class NavigationSystem {
public:
    void update(float dt);

    std::vector<Vec2i> findPath(Vec2i start, Vec2i goal) const;
    bool isWalkable(Vec2i tile) const;
    void setWalkable(Vec2i tile, bool walkable);

private:
    std::unordered_set<Vec2i> m_blockedTiles;
};
