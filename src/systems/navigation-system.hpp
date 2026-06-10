#pragma once

#include "core/math.hpp"
#include <vector>
#include <unordered_set>

class NavigationSystem {
public:
    void update(float dt);

    std::vector<Vec2i> find_path(Vec2i start, Vec2i goal) const;
    bool is_walkable(Vec2i tile) const;
    void set_walkable(Vec2i tile, bool walkable);

private:
    std::unordered_set<Vec2i> blocked_tiles_;
};
