#pragma once

#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include <string>
#include <vector>

class EntityManager;
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
    void update(EntityManager &entities, float dt);

    // Events generated this frame
    std::vector<CombatEvent> const &events() const
    {
        return events_;
    }
    void clear_events()
    {
        events_.clear();
    }

    // Check if any entity on a team is near a position
    bool team_near_position(EntityManager &entities, Team team, Vec2f pos,
                            float radius) const;

    // Spawn a wave of enemies
    void spawn_enemy_wave(EntityManager &entities, int count, Vec2f center,
                          float spread, Team team);

  private:
    std::vector<CombatEvent> events_;

    void resolve_combat(EntityManager &entities, float dt);
    void process_soldier_ai(EntityManager &entities);
    EntityId find_nearest_enemy(EntityManager &entities, EntityId self,
                                Team enemy_team) const;
    int calc_damage(int attack, int defense) const;
};
