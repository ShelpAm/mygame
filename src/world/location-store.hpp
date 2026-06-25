#pragma once

#include "core/math.hpp"
#include "systems/navigation-system.hpp"
#include "world/map-data.hpp"

#include <boost/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Data structures for named locations and routes between them.
// Loaded from assets/data/locations.json on startup.
// ---------------------------------------------------------------------------

struct LocationDefinition {
    std::string id;             // unique key, e.g. "ugarit"
    std::string display_name;   // locale key, e.g. "location.ugarit"
    std::string display_name_en; // English name from JSON "name" field
    Vec2f world_pos;            // world-space centre (from JSON x / y)
    Vec2i tile_center;          // tile coordinate of world_pos
    float discovery_radius = 250.f;
    int building_count = 5;     // how many building tiles to place
    std::string description;    // e.g. "A ruined port..."
};

struct RouteDefinition {
    std::string from_id;
    std::string to_id;
    int danger = 0;
    int travel_time = 0;
};

struct LocationData {
    std::vector<LocationDefinition> locations;
    std::vector<RouteDefinition> routes;
};

/// Load locations.json and return parsed LocationData.
inline LocationData load_locations_from_json(std::string const &path)
{
    LocationData out;

    // Read file into string
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        spdlog::warn("load_locations_from_json: cannot open '{}'", path);
        return out;
    }
    std::fseek(fp, 0, SEEK_END);
    long len = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::string raw(static_cast<size_t>(len), '\0');
    std::fread(raw.data(), 1, static_cast<size_t>(len), fp);
    std::fclose(fp);

    // Parse JSON
    boost::json::value jv = boost::json::parse(raw);
    auto const &root = jv.as_object();

    // Locations
    auto const &j_locs = root.at("locations").as_array();
    out.locations.reserve(j_locs.size());
    for (auto const &j : j_locs) {
        auto const &o = j.as_object();
        LocationDefinition ld;
        ld.id               = o.at("id").as_string().c_str();
        ld.display_name     = "location." + ld.id;
        ld.display_name_en  = o.at("name").as_string().c_str();
        ld.world_pos.x      = static_cast<float>(o.at("x").as_int64());
        ld.world_pos.y      = static_cast<float>(o.at("y").as_int64());
        ld.tile_center      = world_to_tile(ld.world_pos);

        // Size heuristic: larger towns get more building tiles
        if (ld.id == "ugarit")
            ld.building_count = 8;
        else if (ld.id == "byblos")
            ld.building_count = 6;
        else
            ld.building_count = 4;

        // Description from JSON
        if (o.contains("description"))
            ld.description = o.at("description").as_string().c_str();

        out.locations.push_back(std::move(ld));
    }

    // Routes
    if (root.contains("routes")) {
        auto const &j_routes = root.at("routes").as_array();
        out.routes.reserve(j_routes.size());
        for (auto const &j : j_routes) {
            auto const &o = j.as_object();
            RouteDefinition rd;
            rd.from_id     = o.at("from").as_string().c_str();
            rd.to_id       = o.at("to").as_string().c_str();
            rd.danger      = static_cast<int>(o.at("danger").as_int64());
            rd.travel_time = static_cast<int>(o.at("time").as_int64());
            out.routes.push_back(std::move(rd));
        }
    }

    return out;
}

/// Find a LocationDefinition by id; returns nullptr if not found.
inline LocationDefinition const *find_location(
    std::vector<LocationDefinition> const &locs,
    std::string const &id)
{
    for (auto const &ld : locs)
        if (ld.id == id)
            return &ld;
    return nullptr;
}

/// Load terrain.json and populate MapData + NavigationSystem.
/// Each feature tile gets its type set in MapData and is marked
/// non-walkable in the NavigationSystem.
inline void load_terrain_from_json(std::string const &path,
                                    MapData &map_data,
                                    NavigationSystem &nav)
{
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        spdlog::warn("load_terrain_from_json: cannot open '{}'", path);
        return;
    }
    std::fseek(fp, 0, SEEK_END);
    long len = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::string raw(static_cast<size_t>(len), '\0');
    std::fread(raw.data(), 1, static_cast<size_t>(len), fp);
    std::fclose(fp);

    boost::json::value jv = boost::json::parse(raw);
    auto const &features = jv.as_object().at("features").as_array();

    int total = 0;
    for (auto const &f : features) {
        auto const &obj = f.as_object();
        int tile_type = static_cast<int>(obj.at("type").as_int64());
        auto const &tiles = obj.at("tiles").as_array();
        for (auto const &t : tiles) {
            int x = static_cast<int>(t.as_object().at("x").as_int64());
            int y = static_cast<int>(t.as_object().at("y").as_int64());
            if (!map_data.in_bounds(x, y))
                continue;
            auto &td = map_data.tile(x, y);
            td.type = tile_type;
            td.walkable = false;
            td.blocks_vision = (tile_type == 2); // mountains block vision
            nav.set_walkable({x, y}, false);
            ++total;
        }
    }
    spdlog::info("Loaded {} terrain tiles from '{}'", total, path);
}
