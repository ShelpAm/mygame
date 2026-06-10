#pragma once

#include "core/math.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

    // Tile visibility
    bool is_tile_seen(Vec2i tile) const;
    void reveal_tile(Vec2i tile);
    void reveal_radius(Vec2i center, int radius);

    int day() const
    {
        return day_;
    }
    int season() const
    {
        return season_;
    }
    float time_of_day() const
    {
        return time_of_day_;
    }
    std::unordered_set<Vec2i> const &seen_tiles() const
    {
        return seen_tiles_;
    }

    void set_day(int d)
    {
        day_ = d;
    }
    void set_season(int s)
    {
        season_ = s % 4;
    }

  private:
    std::unordered_map<std::string, LocationState> locations_;
    std::unordered_set<Vec2i> seen_tiles_;

    int day_ = 1;
    int season_ = 0;
    float time_of_day_ = 6.f;
    float day_length_ = 24.f;
    float accumulator_ = 0.f;
};
