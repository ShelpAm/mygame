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
    // ---- 0. Pre-compute road tiles so walls can leave gates ----
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

    // ---- 1. Multi-tile building footprints for each town ----
    int next_group = 1;

    for (auto const &loc : locations) {
        Vec2i center = loc.tile_center;

        // Dynamic radius based on building_count — see town_build_radius().
        int radius = town_build_radius(loc.building_count);

        // Collect candidate anchor positions (bottom-left corners).
        std::vector<Vec2i> anchors;
        for (int dy = -radius; dy < radius; ++dy)
            for (int dx = -radius; dx < radius; ++dx) {
                Vec2i t{center.x + dx, center.y + dy};
                if (map_data.in_bounds(t.x, t.y) &&
                    map_data.tile(t.x, t.y).type == TileType::grass)
                    anchors.push_back(t);
            }

        if (anchors.empty())
            continue;

        // Seeded RNG for organic-but-deterministic layout.
        std::seed_seq seed_seq{loc.id.begin(), loc.id.end()};
        std::mt19937 rng(seed_seq);
        std::shuffle(anchors.begin(), anchors.end(), rng);

        // We distribute three footprint sizes:
        //   2×2 — large buildings (inn, temple, warehouse)
        //   2×1 — medium buildings (market, forge, longhouse)
        //   1×1 — small huts / filler
        // The pool cycles through sizes with a bias so every town gets
        // a mix of large and small structures.
        static constexpr int size_pool[] = {
            2, 2, // 2×2
            1,    // 2×1
            0, 0, // 1×1 filler
        };
        auto pick_size = [&]() -> std::pair<int, int> {
            switch (size_pool[std::uniform_int_distribution(0, (int)std::size(size_pool) - 1)(rng)]) {
            case 2:  return {2, 2};
            case 1:  return {2, 1};
            default: return {1, 1};
            }
        };

        std::vector<Vec2i> occupied;
        occupied.push_back(center); // keep centre as walkable town square

        int placed = 0;
        int attempts = 0;
        while (placed < loc.building_count && attempts < loc.building_count * 8) {
            ++attempts;
            auto [w, h] = pick_size();

            // Find a valid anchor from the shuffled list.
            bool success = false;
            for (auto const &anchor : anchors) {
                // Check that every tile in the footprint is valid and
                // maintains a minimum 1-tile gap from any already-placed
                // tile (including the centre square) so that there are
                // clear walkable paths between buildings.
                bool ok = true;
                for (int dy2 = 0; dy2 < h && ok; ++dy2)
                    for (int dx2 = 0; dx2 < w && ok; ++dx2) {
                        Vec2i t{anchor.x + dx2, anchor.y + dy2};
                        if (!map_data.in_bounds(t.x, t.y) ||
                            map_data.tile(t.x, t.y).type != TileType::grass)
                            ok = false;
                        for (auto const &o : occupied)
                            if (std::abs(t.x - o.x) <= 1 &&
                                std::abs(t.y - o.y) <= 1)
                                ok = false;
                    }
                if (!ok)
                    continue;

                // Place the footprint.
                for (int dy2 = 0; dy2 < h; ++dy2)
                    for (int dx2 = 0; dx2 < w; ++dx2) {
                        Vec2i t{anchor.x + dx2, anchor.y + dy2};
                        auto &td = map_data.tile(t.x, t.y);
                        td.type = TileType::building;
                        td.walkable = false;
                        td.blocks_vision = true;
                        td.building_group = next_group;
                        nav.set_walkable(t, false);
                        occupied.push_back(t);
                    }
                ++next_group;
                ++placed;
                success = true;
                break;
            }
        }

        // Town wall: 2 tiles outside the building cluster for a spacious feel.
        int wall_radius = radius + 2;
        for (int dy = -wall_radius; dy <= wall_radius; ++dy) {
            for (int dx = -wall_radius; dx <= wall_radius; ++dx) {
                // Only the outermost ring.
                if (std::abs(dx) <= wall_radius - 1 &&
                    std::abs(dy) <= wall_radius - 1)
                    continue;

                Vec2i tile{center.x + dx, center.y + dy};
                if (!map_data.in_bounds(tile.x, tile.y))
                    continue;
                if (map_data.tile(tile.x, tile.y).type != TileType::grass)
                    continue;

                // Leave a 1-tile gate where a road passes.
                if (road_tiles.contains(tile))
                    continue;

                auto &td = map_data.tile(tile.x, tile.y);
                td.type = TileType::wall;
                td.walkable = false;
                td.blocks_vision = true;
                nav.set_walkable(tile, false);
            }
        }
    }

    spdlog::info("generate_town_footprints: {} building groups across {} towns",
                 next_group - 1, locations.size());

    // ---- 3. Road tiles between connected towns ----
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
        for (auto const &tile : tiles) {
            if (!map_data.in_bounds(tile.x, tile.y))
                continue;

            auto &td = map_data.tile(tile.x, tile.y);

            // Only overwrite plain tiles (grass).
            if (td.type != TileType::grass)
                continue;

            td.type = TileType::road;
            td.walkable = true;
            td.blocks_vision = false;
        }
    }
}
