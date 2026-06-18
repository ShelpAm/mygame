#pragma once

#include "core/math.hpp"
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

inline std::unordered_set<Vec2i>
compute_visible_arc(Vec2i center, int radius, Vec2f facing, float arc_deg)
{
    std::unordered_set<Vec2i> result;
    double half = arc_deg * 0.5 * std::numbers::pi / 180.0;
    double cos_half = std::cos(half);
    int r2 = radius * radius;

    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            int d2 = dy * dy + dx * dx;
            if (d2 > r2)
                continue;
            if (d2 == 0) {
                result.insert(center);
                continue;
            }
            double dist = std::sqrt(static_cast<double>(d2));
            double dot = (dx / dist) * facing.x + (dy / dist) * facing.y;
            if (dot >= cos_half)
                result.insert({center.x + dx, center.y + dy});
        }
    return result;
}

enum class TileVisibility { Unexplored, Explored, Visible };

struct PlayerVisibility {
    std::unordered_set<Vec2i> visible;
    std::unordered_set<Vec2i> explored;

    TileVisibility query(Vec2i tile) const
    {
        if (visible.contains(tile))
            return TileVisibility::Visible;
        if (explored.contains(tile))
            return TileVisibility::Explored;
        return TileVisibility::Unexplored;
    }

    void explore(Vec2i tile) { explored.insert(tile); }

    void explore_radius(Vec2f center_world, float radius_world);

    void set_visible_arc(Vec2i center, int radius, Vec2f facing, float arc_deg);
    void set_visible_from_center(Vec2i center, int radius);
    void clear_visible() { visible.clear(); }

    bool is_explored(Vec2i tile) const { return explored.contains(tile); }
    bool is_visible(Vec2i tile) const { return visible.contains(tile); }
};

class WorldState {
  public:
    void update(float dt);

    struct LocationState {
        std::string name;
        std::string region_id;
        bool player_has_visited = false;
        int last_visit_day = -1;
        std::string controlling_faction_id;
        int population = 0;
    };

    LocationState const *location(std::string const &id) const;
    LocationState *location_mutable(std::string const &id);
    void add_location(std::string const &id, LocationState state);

    // Tile visibility — three-state model
    bool is_tile_explored(Vec2i tile) const;
    bool is_tile_visible(Vec2i tile) const;
    TileVisibility tile_visibility(Vec2i tile) const;

    void explore_tile(Vec2i tile);
    void explore_radius(Vec2f center_world, float radius_world);
    void reveal_tile(Vec2i tile) { explore_tile(tile); }

    void set_visible_tiles_from_center(Vec2i center, int radius);
    void set_visible_arc(Vec2i center, int radius, Vec2f facing, float arc_deg);
    void clear_visible_tiles();

    std::unordered_set<Vec2i> const &explored_tiles() const
    {
        return explored_tiles_;
    }
    std::unordered_set<Vec2i> const &visible_tiles() const
    {
        return visible_tiles_;
    }
    std::unordered_set<Vec2i> const &seen_tiles() const
    {
        return explored_tiles_;
    }

    int day() const { return day_; }
    int season() const { return season_; }
    float time_of_day() const { return time_of_day_; }

    void set_day(int d) { day_ = d; }
    void set_season(int s) { season_ = s % 4; }
    void set_time_of_day(float t) { time_of_day_ = t; }

  private:
    std::unordered_map<std::string, LocationState> locations_;
    std::unordered_set<Vec2i> explored_tiles_;
    std::unordered_set<Vec2i> visible_tiles_;

    static constexpr float day_length = 24.F;
    int day_ = 1;
    int season_ = 0;
    float time_of_day_ = 6.F;
    float accumulator_ = 0.F;
};
