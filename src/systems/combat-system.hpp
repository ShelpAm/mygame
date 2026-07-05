#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include <flecs.h>
#include <functional>
#include <unordered_map>
#include <vector>

class CombatSystem {
  public:
    std::vector<CombatEvent> const &events() const { return events_; }
    std::vector<CombatEvent> consume_events() { return std::exchange(events_, {}); }

    bool team_near_position(flecs::world &world, Team team, Vec2f pos, float radius) const;

    void spawn_enemy_wave(flecs::world &world, int count, Vec2f center, float spread, Team team,
                          std::vector<EntityId> *out_ids = nullptr);

    void set_dirty_callback(std::function<void(EntityId)> cb) { dirty_cb_ = std::move(cb); }

  private:
    std::vector<CombatEvent> events_;
    std::function<void(EntityId)> dirty_cb_;
};
