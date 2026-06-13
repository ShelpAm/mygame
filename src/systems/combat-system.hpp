#pragma once

#include "entities/components/combat-stats.hpp"
#include "entities/entity-manager.hpp"
#include <flecs.h>
#include <string>
#include <vector>

struct Vec2f;

struct CombatEvent {
    EntityId attacker_id = 0;
    EntityId defender_id = 0;
    std::string attacker_name;
    std::string defender_name;
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
    void clear_events()
    {
        events_.clear();
    }

    bool team_near_position(flecs::world &world, Team team, Vec2f pos,
                            float radius) const;

    void spawn_enemy_wave(flecs::world &world, int count, Vec2f center,
                          float spread, Team team);

  private:
    std::vector<CombatEvent> events_;

    void resolve_combat(flecs::world &world, float dt);
    void process_soldier_ai(flecs::world &world);
    EntityId find_nearest_enemy(flecs::world &world, EntityId self,
                                Team enemy_team) const;
    int calc_damage(int attack, int defense) const;
};
