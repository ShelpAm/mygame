#pragma once

#include "core/math.hpp"
#include <vector>

inline constexpr float tile_size = 64.F;

inline Vec2f lu_of_tile(Vec2i tile)
{
    return {static_cast<float>(tile.x) * tile_size, static_cast<float>(tile.y) * tile_size};
}

inline Vec2f center_of_tile(Vec2i tile)
{
    constexpr auto half = 0.5F;
    return {(static_cast<float>(tile.x) + half) * tile_size,
            (static_cast<float>(tile.y) + half) * tile_size};
}

inline Vec2i world_to_tile(Vec2f pos)
{
    return {static_cast<int>(std::floor(pos.x / tile_size)),
            static_cast<int>(std::floor(pos.y / tile_size))};
}

enum class TileType : int {
    grass = 0,
    water = 1,
    mountain = 2,
    road = 3,
    building = 4,
    wall = 5,
};

struct TileData {
    TileType type = TileType::grass;
    bool walkable = true;
    bool blocks_vision = false;
    int building_group = 0; // 0=none; >0 groups tiles belonging to one building
};

class MapData {
  public:
    MapData(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height);

    TileData &tile(std::int32_t x, std::int32_t y);
    TileData const &tile(std::int32_t x, std::int32_t y) const;
    bool in_bounds(std::int32_t x, std::int32_t y) const;

    std::int32_t width() const { return w_; }
    std::int32_t height() const { return h_; }

  private:
    std::int32_t x_, y_, w_, h_;
    std::vector<std::vector<TileData>> tiles_;
};
