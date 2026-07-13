#pragma once

#include <cstdint>
#include <string>

struct BuildingData {
    enum class Type { inn, market, temple, blacksmith, generic };
    Type type = Type::generic;
    std::string town_id;      // which town this belongs to, e.g. "thornhaven"
    std::string display_name; // e.g. "The Rusty Nail Inn"
    int group_id = 0;         // matches TileData::building_group on the tilemap
    uint8_t width_tiles = 1;  // footprint width in tiles
    uint8_t height_tiles = 1; // footprint height in tiles
};
