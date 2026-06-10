#pragma once

#include <vector>
#include <string>

struct TileData {
    int type = 0;  // 0=grass, 1=water, 2=mountain, 3=road, 4=building
    bool walkable = true;
    bool blocksVision = false;
};

class MapData {
public:
    MapData(int width, int height);

    TileData& tile(int x, int y);
    const TileData& tile(int x, int y) const;
    bool inBounds(int x, int y) const;

    int width() const { return m_width; }
    int height() const { return m_height; }

    static constexpr int TILE_SIZE = 64;

private:
    int m_width, m_height;
    std::vector<TileData> m_tiles;
};
