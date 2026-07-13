#pragma once

#include "core/math.hpp"
#include "systems/navigation-system.hpp"
#include "world/location-store.hpp"
#include "world/map-data.hpp"

#include <algorithm>
#include <cstdlib>
#include <random>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Terrain generation: converts location definitions into MapData tiles
// (building clusters + road routes) and updates NavigationSystem blocked
// tiles accordingly.
//
// Called once at startup on both server and client.
// ---------------------------------------------------------------------------

namespace terrain_detail {

/// Bresenham line: return all tile coordinates from a to b (inclusive).
inline std::vector<Vec2i> line_tiles(Vec2i a, Vec2i b)
{
    std::vector<Vec2i> tiles;
    int dx = std::abs(b.x - a.x);
    int dy = std::abs(b.y - a.y);
    int sx = a.x < b.x ? 1 : -1;
    int sy = a.y < b.y ? 1 : -1;
    int err = dx - dy;

    int x = a.x;
    int y = a.y;
    while (true) {
        tiles.push_back({x, y});
        if (x == b.x && y == b.y)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
    return tiles;
}

} // namespace terrain_detail

/// Generate town building clusters and road connections in the map.
///
/// @param map_data  Client/server tile grid to modify.
/// @param nav       Navigation system whose blocked-tile set will be updated.
/// @param locations  Location definitions with world positions and sizes.
/// @param routes     Route definitions connecting locations.
inline void generate_town_footprints(MapData &map_data, NavigationSystem &nav,
                                     std::vector<LocationDefinition> const &locations,
                                     std::vector<RouteDefinition> const &routes)
{
    // ---- 1. 预计算并收集所有道路格子 (消除后续的重复计算) ----
    std::unordered_set<Vec2i> road_tiles;
    for (auto const &route : routes) {
        LocationDefinition const *from_loc = nullptr;
        LocationDefinition const *to_loc = nullptr;
        for (auto const &loc : locations) {
            if (loc.id == route.from_id)
                from_loc = &loc;
            if (loc.id == route.to_id)
                to_loc = &loc;
        }
        if (!from_loc || !to_loc)
            continue;

        auto tiles = terrain_detail::line_tiles(from_loc->tile_center, to_loc->tile_center);
        road_tiles.insert(tiles.begin(), tiles.end());
    }

    // ---- 2. 为每个城镇生成多图块建筑占地 ----
    int next_group = 1;

    for (auto const &loc : locations) {
        Vec2i const center = loc.tile_center;
        int const radius = town_build_radius(loc.building_count);

        // 收集候选锚点 (左下角)
        std::vector<Vec2i> anchors;
        for (int dy = -radius; dy < radius; ++dy) {
            for (int dx = -radius; dx < radius; ++dx) {
                Vec2i t{center.x + dx, center.y + dy};
                if (map_data.in_bounds(t.x, t.y) &&
                    map_data.tile(t.x, t.y).type == TileType::grass) {
                    anchors.push_back(t);
                }
            }
        }

        if (anchors.empty())
            continue;

        // 使用基于城镇 ID 的种子，确保有机的确定性布局
        std::seed_seq seed_seq{loc.id.begin(), loc.id.end()};
        std::mt19937 rng(seed_seq);
        std::shuffle(anchors.begin(), anchors.end(), rng);

        // 建筑尺寸抽取池
        static constexpr std::array<Vec2i, 10> size_pool{
            Vec2i{3, 3}, {3, 3}, {2, 2}, {2, 2}, {2, 2}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1},
        };
        auto dist = std::uniform_int_distribution(0, static_cast<int>(std::size(size_pool)) - 1);
        auto pick_size = [&]() { return size_pool[static_cast<std::size_t>(dist(rng))]; };

        // 核心优化：利用网格自身状态进行 O(1) 碰撞及间距检测
        // 原本需要遍历整个 occupied 数组，现在直接根据网格类型和局部 3x3 邻域判定
        auto can_fit_building = [&](Vec2i const &anchor, int w, int h) -> bool {
            for (int dy2 = 0; dy2 < h; ++dy2) {
                for (int dx2 = 0; dx2 < w; ++dx2) {
                    Vec2i t{anchor.x + dx2, anchor.y + dy2};

                    // 1. 边界与基础地形有效性检查
                    if (!map_data.in_bounds(t.x, t.y) ||
                        map_data.tile(t.x, t.y).type != TileType::grass) {
                        return false;
                    }

                    // 2. 检查与城镇中心的间距 (保持中心广场可通行)
                    if (std::abs(t.x - center.x) <= 1 && std::abs(t.y - center.y) <= 1) {
                        return false;
                    }

                    // 3. 检查 3x3 邻域内是否有其他建筑 (保证建筑之间有 1 格的过道)
                    for (int ny = -1; ny <= 1; ++ny) {
                        for (int nx = -1; nx <= 1; ++nx) {
                            int check_x = t.x + nx;
                            int check_y = t.y + ny;
                            if (map_data.in_bounds(check_x, check_y)) {
                                if (map_data.tile(check_x, check_y).type == TileType::building) {
                                    return false; // 距离其他建筑太近
                                }
                            }
                        }
                    }
                }
            }
            return true;
        };

        // 开始放置建筑
        int placed = 0;
        int attempts = 0;
        int const max_attempts = loc.building_count * 8;

        while (placed < loc.building_count && attempts < max_attempts) {
            ++attempts;
            auto [w, h] = pick_size();

            for (auto const &anchor : anchors) {
                if (!can_fit_building(anchor, w, h)) {
                    continue;
                }

                // 确定可行，实施放置并标记数据
                for (int dy2 = 0; dy2 < h; ++dy2) {
                    for (int dx2 = 0; dx2 < w; ++dx2) {
                        Vec2i t{anchor.x + dx2, anchor.y + dy2};
                        auto &td = map_data.tile(t.x, t.y);

                        td.type = TileType::building;
                        td.walkable = false;
                        td.blocks_vision = true;
                        td.building_group = next_group;
                        nav.set_walkable(t, false);
                    }
                }
                ++next_group;
                ++placed;
                break; // 当前建筑放置成功，跳出换下一个
            }
        }

        // ---- 3. 生成城镇围墙 (外扩 2 个图块以维持空间感) ----
        int const wall_radius = radius + 2;
        for (int dy = -wall_radius; dy <= wall_radius; ++dy) {
            for (int dx = -wall_radius; dx <= wall_radius; ++dx) {
                // 仅处理最外层的闭合环
                if (std::abs(dx) <= wall_radius - 1 && std::abs(dy) <= wall_radius - 1) {
                    continue;
                }

                Vec2i tile{center.x + dx, center.y + dy};
                if (!map_data.in_bounds(tile.x, tile.y))
                    continue;

                auto &td = map_data.tile(tile.x, tile.y);
                if (td.type != TileType::grass)
                    continue;

                // 如果该墙面有预计算的道路穿过，则留出 1 跨度的城门
                if (road_tiles.contains(tile)) {
                    continue;
                }

                td.type = TileType::wall;
                td.walkable = false;
                td.blocks_vision = true;
                nav.set_walkable(tile, false);
            }
        }
    }

    spdlog::info("generate_town_footprints: {} building groups across {} towns", next_group - 1,
                 locations.size());

    // ---- 4. 铺设城镇间的实体道路 (直接复用预计算结果) ----
    for (auto const &tile : road_tiles) {
        if (!map_data.in_bounds(tile.x, tile.y))
            continue;

        auto &td = map_data.tile(tile.x, tile.y);
        // 道路属于底层地表，只覆盖纯草地，不破坏建筑和城墙
        if (td.type == TileType::grass) {
            td.type = TileType::road;
            td.walkable = true;
            td.blocks_vision = false;
        }
    }
}
