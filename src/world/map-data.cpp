#include "world/map-data.hpp"
MapData::MapData(int width, int height)
    : width_(width), height_(height), tiles_(width * height)
{}

TileData& MapData::tile(int x, int y) {
    return tiles_[y * width_ + x];
}

const TileData& MapData::tile(int x, int y) const {
    return tiles_[y * width_ + x];
}

bool MapData::in_bounds(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}
