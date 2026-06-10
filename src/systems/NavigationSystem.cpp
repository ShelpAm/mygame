#include "systems/NavigationSystem.hpp"
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cmath>

void NavigationSystem::update(float /*dt*/) {}

bool NavigationSystem::isWalkable(Vec2i tile) const {
    return !m_blockedTiles.contains(tile);
}

void NavigationSystem::setWalkable(Vec2i tile, bool walkable) {
    if (walkable) {
        m_blockedTiles.erase(tile);
    } else {
        m_blockedTiles.insert(tile);
    }
}

std::vector<Vec2i> NavigationSystem::findPath(Vec2i start, Vec2i goal) const {
    if (!isWalkable(goal)) return {};

    using Node = std::pair<int, Vec2i>;
    auto heuristic = [](Vec2i a, Vec2i b) -> int {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<>> openSet;
    std::unordered_map<Vec2i, Vec2i, std::hash<Vec2i>> cameFrom;
    std::unordered_map<Vec2i, int, std::hash<Vec2i>> gScore;

    gScore[start] = 0;
    openSet.emplace(heuristic(start, goal), start);

    const Vec2i neighbors[] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};

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
            if (!isWalkable(next)) continue;

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
