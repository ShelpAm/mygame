#include "world/map-data.hpp"
MapData::MapData(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height)
    : x_(x), y_(y), w_(static_cast<std::int32_t>(width)), h_(static_cast<std::int32_t>(height)),
      tiles_(height, std::vector<TileData>(width))
{
}

TileData &MapData::tile(std::int32_t x, std::int32_t y)
{
    if (!in_bounds(x, y))
        throw std::out_of_range("MapData::tile: coordinates out of bounds");

    return tiles_[x - x_][y - y_]; // NOLINT
}

TileData const &MapData::tile(std::int32_t x, std::int32_t y) const
{
    return const_cast<MapData *>(this)->tile(x, y); // NOLINT
}

bool MapData::in_bounds(std::int32_t x, std::int32_t y) const
{
    return x >= x_ && x < x_ + w_ && y >= y_ && y < y_ + h_;
}
