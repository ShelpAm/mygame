#pragma once

#include "core/math.hpp"
#include "systems/navigation-system.hpp"
#include "world/location-store.hpp"
#include "world/map-data.hpp"

#include <cstdlib>
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
inline void generate_town_footprints(
    MapData &map_data,
    NavigationSystem &nav,
    std::vector<LocationDefinition> const &locations,
    std::vector<RouteDefinition> const &routes)
{
    // ---- 1. Building tiles for each town ----
    for (auto const &loc : locations) {
        Vec2i center = loc.tile_center;

        // Place buildings in a compact cluster around the town centre.
        // The cluster is roughly (radius+1) tiles from centre so that
        // the town has a recognisable footprint.
        int radius = (loc.building_count > 6) ? 2 : 1;

        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                // Skip the centre tile itself (reserved for eventual
                // town-square or spawn point) and avoid too many tiles.
                if (dx == 0 && dy == 0)
                    continue;
                if (!map_data.in_bounds(center.x + dx, center.y + dy))
                    continue;
                // Don't overwrite existing terrain features (water, mountain)
                if (map_data.tile(center.x + dx, center.y + dy).type != 0)
                    continue;

                // Deterministic placement based on town seed so the
                // same JSON always produces the same layout.
                int seed = loc.id[0] * 31 + loc.id.size();
                int hash = ((center.x + dx) * 73 + (center.y + dy) * 137) * seed;
                // Roughly building_count out of the cluster survive
                int max_buildings = (radius * 2 + 1) * (radius * 2 + 1) - 1;
                if (std::abs(hash) % max_buildings >= loc.building_count)
                    continue;

                Vec2i tile{center.x + dx, center.y + dy};
                auto &td = map_data.tile(tile.x, tile.y);
                td.type = 4;          // building
                td.walkable = false;
                td.blocks_vision = true;
                nav.set_walkable(tile, false);
            }
        }
    }

    // ---- 2. Road tiles between connected towns ----
    for (auto const &route : routes) {
        // Find tile centers for both endpoints
        LocationDefinition const *from_loc = nullptr;
        LocationDefinition const *to_loc = nullptr;
        for (auto const &loc : locations) {
            if (loc.id == route.from_id) from_loc = &loc;
            if (loc.id == route.to_id)   to_loc = &loc;
        }
        if (!from_loc || !to_loc)
            continue;

        auto road_tiles = terrain_detail::line_tiles(from_loc->tile_center, to_loc->tile_center);
        for (auto const &tile : road_tiles) {
            if (!map_data.in_bounds(tile.x, tile.y))
                continue;

            auto &td = map_data.tile(tile.x, tile.y);

            // Only overwrite plain tiles (type 0 = grass) so that
            // existing building or water tiles keep their identity.
            if (td.type != 0)
                continue;

            td.type = 3;          // road
            td.walkable = true;
            td.blocks_vision = false;
            // nav.set_walkable(tile, true) — not needed: grass is already walkable
        }
    }
}
