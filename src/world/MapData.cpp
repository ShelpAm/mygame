#include "world/MapData.hpp"

MapData::MapData(int width, int height)
    : m_width(width), m_height(height), m_tiles(width * height)
{}

TileData& MapData::tile(int x, int y) {
    return m_tiles[y * m_width + x];
}

const TileData& MapData::tile(int x, int y) const {
    return m_tiles[y * m_width + x];
}

bool MapData::inBounds(int x, int y) const {
    return x >= 0 && x < m_width && y >= 0 && y < m_height;
}
