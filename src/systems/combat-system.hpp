#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include <flecs.h>
#include <functional>
#include <unordered_map>
#include <vector>

struct CombatEvent {
    EntityId attacker_id = 0;
    EntityId defender_id = 0;
    int damage = 0;
    bool killed = false;
};

class CombatSystem {
  public:
    void update(flecs::world &world, float dt);

    std::vector<CombatEvent> const &events() const
    {
        return events_;
    }
    std::vector<CombatEvent> consume_events()
    {
        return std::exchange(events_, {});
    }

    bool team_near_position(flecs::world &world, Team team, Vec2f pos,
                            float radius) const;

    void spawn_enemy_wave(flecs::world &world, int count, Vec2f center,
                          float spread, Team team,
                          std::vector<EntityId> *out_ids = nullptr);

    void set_dirty_callback(std::function<void(EntityId)> cb)
    {
        dirty_cb_ = std::move(cb);
    }

  private:
    std::vector<CombatEvent> events_;
    std::function<void(EntityId)> dirty_cb_;

    void resolve_combat(flecs::world &world, float dt);
    void process_soldier_ai(flecs::world &world);
    EntityId find_nearest_enemy(
        flecs::world &world, EntityId self, Team enemy_team,
        std::unordered_map<EntityId, int> const &extra_damage = {}) const;
    int calc_damage(int attack, int defense) const;
};
