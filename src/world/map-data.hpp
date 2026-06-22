#pragma once

#include "core/math.hpp"
#include <vector>

inline constexpr float tile_size = 64.F;

inline Vec2f lu_of_tile(Vec2i tile)
{
    return {static_cast<float>(tile.x) * tile_size,
            static_cast<float>(tile.y) * tile_size};
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

struct TileData {
    int type = 0; // 0=grass, 1=water, 2=mountain, 3=road, 4=building
    bool walkable = true;
    bool blocks_vision = false;
};

class MapData {
  public:
    MapData(int width, int height);

    TileData &tile(int x, int y);
    TileData const &tile(int x, int y) const;
    bool in_bounds(int x, int y) const;

    int width() const { return width_; }
    int height() const { return height_; }

  private:
    int width_, height_;
    std::vector<TileData> tiles_;
};
