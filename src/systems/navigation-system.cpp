#include "systems/navigation-system.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

void NavigationSystem::update(float /*dt*/)
{
}

bool NavigationSystem::is_walkable(Vec2i tile) const
{
    return !blocked_tiles_.contains(tile);
}

void NavigationSystem::set_walkable(Vec2i tile, bool walkable)
{
    if (walkable) {
        blocked_tiles_.erase(tile);
    }
    else {
        blocked_tiles_.insert(tile);
    }
}

std::vector<Vec2i> NavigationSystem::find_path(Vec2i start, Vec2i goal) const
{
    if (!is_walkable(goal))
        return {};

    using Node = std::pair<int, Vec2i>;
    auto heuristic = [](Vec2i a, Vec2i b) -> int {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<>> openSet;
    std::unordered_map<Vec2i, Vec2i, std::hash<Vec2i>> cameFrom;
    std::unordered_map<Vec2i, int, std::hash<Vec2i>> gScore;

    gScore[start] = 0;
    openSet.emplace(heuristic(start, goal), start);

    Vec2i const neighbors[] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};

    while (!openSet.empty()) {
        auto [f, current] = openSet.top();
        openSet.pop();

        if (current == goal) {
            std::vector<Vec2i> path;
            for (Vec2i at = goal; !(at == start); at = cameFrom[at]) {
                path.push_back(at);
            }
            path.push_back(start);
            std::ranges::reverse(path);
            return path;
        }

        for (auto [dx, dy] : neighbors) {
            Vec2i next{current.x + dx, current.y + dy};
            if (!is_walkable(next))
                continue;

            int tentativeG = gScore[current] + 1;
            if (!gScore.contains(next) || tentativeG < gScore[next]) {
                cameFrom[next] = current;
                gScore[next] = tentativeG;
                openSet.emplace(tentativeG + heuristic(next, goal), next);
            }
        }
    }

    return {};
}
