#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/combat-system.hpp"
#include "systems/navigation-system.hpp"
#include <flecs.h>
#include <functional>
#include <unordered_map>
#include <vector>

struct Projectile {
    Vec2f pos;
    Vec2f direction;
    EntityId target_id = 0;
    int damage = 0;
    float speed = 400.f;
    float total_dist = 0.f;
    float traveled = 0.f;
};

int calc_damage(int attack, int defense);

/// time-complexity: O(n)
/// detection_range overrides attack_range as the max search radius (0 = use
/// attack_range).
EntityId
find_nearest_enemy(flecs::world &world, EntityId self,
                   std::unordered_map<EntityId, int> const &extra_damage = {},
                   float detection_range = 0.F);

// Shared survival decay logic — used by SurvivalDecay system + testable
// standalone.
void decay_survival(SurvivalState &s, float dt);

// Shared soldier AI — sets velocity on Movement component; the Movement
// system applies position += velocity * dt each frame.
// Uses A* pathfinding (nav) to navigate around blocked tiles.
// Three stances:
//   passive   — follow formation only, never attack
//   defensive — follow formation, attack enemies within attack_range
//   offensive — hunt enemies within engage_range, fall back to formation
/// Note that this function depends on the value of `ai.formation_offset`
void run_soldier_ai(flecs::world &world, flecs::entity e, SoldierAI &ai,
                    Transform &pos, Movement &mov, CombatStats &cs,
                    NavigationSystem const *nav, float dt,
                    std::function<void(EntityId)> const &mark_dirty);

// Shared combat batch — used by CombatResolution system + testable standalone.
void run_combat_batch(flecs::world &world, float dt,
                      std::vector<CombatEvent> &out_events,
                      std::vector<Projectile> &out_projectiles,
                      std::function<void(EntityId)> const &mark_dirty);

void update_projectiles(flecs::world &world,
                        std::vector<Projectile> &projectiles, float dt,
                        std::vector<CombatEvent> &out_events);
