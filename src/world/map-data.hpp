#pragma once

#include <string>
#include <vector>

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

    int width() const
    {
        return width_;
    }
    int height() const
    {
        return height_;
    }

    static constexpr int tile_size = 64;

  private:
    int width_, height_;
    std::vector<TileData> tiles_;
};
