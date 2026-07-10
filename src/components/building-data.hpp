#pragma once

#include <string>

struct BuildingData {
    enum class Type { inn, market, temple, blacksmith, generic };
    Type type = Type::generic;
    std::string town_id;     // which town this belongs to, e.g. "thornhaven"
    std::string display_name; // e.g. "The Rusty Nail Inn"
    int group_id = 0;        // matches TileData::building_group on the tilemap
};
