#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/combat-system.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <flecs.h>
#include <unordered_map>

struct Projectile {
    Vec2f pos;
    Vec2f direction;
    EntityId target_id = 0;
    int damage = 0;
    float speed = 400.f;
    float total_dist = 0.f;
    float traveled = 0.f;
};

inline int calc_damage(int attack, int defense)
{
    int base = std::max(1, attack - defense / 2);
    int variance = std::rand() % 3 - 1;
    return std::max(1, base + variance);
}

/// time-complexity: O(n)
/// detection_range overrides attack_range as the max search radius (0 = use
/// attack_range).
inline EntityId
find_nearest_enemy(flecs::world &world, EntityId self, Team my_team,
                   std::unordered_map<EntityId, int> const &extra_damage = {},
                   float detection_range = 0.f)
{
    flecs::entity self_e = world.entity(self);
    auto const *selfPos = self_e.try_get<Position>();
    auto const *selfStats = self_e.try_get<CombatStats>();
    if (!selfPos || !selfStats)
        return 0;

    EntityId nearest = 0;
    float nearestDist =
        detection_range > 0.f ? detection_range : selfStats->attack_range;

    world.query<CombatStats, Position>().each(
        [&](flecs::entity e, CombatStats &cs, Position &pos) {
            if (e.id() == self || !cs.alive || !is_hostile(my_team, cs.team))
                return;
            auto it = extra_damage.find(e.id());
            if (it != extra_damage.end() && cs.hp - it->second <= 0)
                return;
            float d = std::hypot(pos.world_pos.x - selfPos->world_pos.x,
                                 pos.world_pos.y - selfPos->world_pos.y);
            if (d < nearestDist) {
                nearestDist = d;
                nearest = e.id();
            }
        });
    return nearest;
}

// Shared survival decay logic — used by SurvivalDecay system + testable
// standalone.
inline void decay_survival(SurvivalState &s, float dt)
{
    float days_passed = dt / 24.f;
    s.food -= SurvivalState::food_decay_per_day * days_passed;
    s.water -= SurvivalState::water_decay_per_day * days_passed;
    if (s.food <= 0.f)
        s.health -= SurvivalState::health_decay_starving * days_passed;
    if (s.water <= 0.f)
        s.health -= SurvivalState::health_decay_starving * days_passed;
    s.energy -= 0.5f * dt;
    s.food = std::clamp(s.food, 0.f, 100.f);
    s.water = std::clamp(s.water, 0.f, 100.f);
    s.health = std::clamp(s.health, 0.f, 100.f);
    s.energy = std::clamp(s.energy, 0.f, 100.f);
}

// Shared soldier AI — sets velocity on Movement component; the Movement
// system applies position += velocity * dt each frame.
template <typename DirtyFn>
inline void run_soldier_ai(flecs::world &world, flecs::entity e, SoldierAI &ai,
                           Position &pos, Movement &mov, CombatStats &cs,
                           DirtyFn &&mark_dirty)
{
    if (!cs.alive) {
        mov.velocity = {0, 0};
        return;
    }
    bool changed = false;

    auto enemyId =
        find_nearest_enemy(world, e.id(), cs.team, {}, ai.engage_range);
    if (enemyId != 0) {
        auto const enemyPos = world.entity(enemyId).get<Position>();
        Vec2f diff = enemyPos.world_pos - pos.world_pos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
        if (dist <= ai.engage_range) {
            if (dist > cs.attack_range) {
                Vec2f dir = {diff.x / dist, diff.y / dist};
                mov.velocity = dir * mov.speed * 1.2f;
                changed = true;
            }
            else {
                mov.velocity = {0, 0};
            }
            if (!ai.in_combat) {
                ai.in_combat = true;
                changed = true;
            }
            if (changed)
                mark_dirty(e.id());
            return;
        }
    }

    if (ai.in_combat) {
        ai.in_combat = false;
        changed = true;
    }

    // Guard stance: return to guard post after combat, stand still otherwise
    if (ai.stance == SoldierStance::guard) {
        Vec2f d = ai.guard_post - pos.world_pos;
        float dist = std::sqrt(d.x * d.x + d.y * d.y);
        if (dist > ai.follow_distance) {
            Vec2f dir = d.x == 0 && d.y == 0 ? Vec2f{1.f, 0.f}
                                             : Vec2f{d.x / dist, d.y / dist};
            mov.velocity = dir * mov.speed;
            changed = true;
        }
        else {
            mov.velocity = {0, 0};
        }
        if (changed)
            mark_dirty(e.id());
        return;
    }

    // follow or patrol: follow leader in formation
    auto const *leaderPos = world.entity(ai.follow_target).try_get<Position>();
    if (!leaderPos) {
        mov.velocity = {0, 0};
        if (changed)
            mark_dirty(e.id());
        return;
    }

    Vec2f goal = leaderPos->world_pos + ai.formation_offset;
    Vec2f diff = goal - pos.world_pos;
    float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);

    if (dist > ai.follow_distance) {
        Vec2f dir = diff.x == 0 && diff.y == 0
                        ? Vec2f{1.f, 0.f}
                        : Vec2f{diff.x / dist, diff.y / dist};
        mov.velocity = dir * mov.speed;
        changed = true;
    }
    else {
        mov.velocity = {0, 0};
    }
    if (changed)
        mark_dirty(e.id());
}

// Shared combat batch — used by CombatResolution system + testable standalone.
template <typename DirtyFn>
inline void run_combat_batch(flecs::world &world, float dt,
                             std::vector<CombatEvent> &out_events,
                             std::vector<Projectile> &out_projectiles,
                             DirtyFn &&mark_dirty)
{
    std::vector<flecs::entity> combatants;
    world.query<CombatStats, Position>().each(
        [&](flecs::entity e, CombatStats &cs, Position &) {
            if (cs.alive && cs.team != Team::neutral)
                combatants.push_back(e);
        });

    std::unordered_map<EntityId, int> damage_dealt;
    std::unordered_map<EntityId, float> new_cooldowns;

    for (auto &attacker : combatants) {
        auto const *atkStats = attacker.try_get<CombatStats>();
        auto const *atkPos = attacker.try_get<Position>();
        if (!atkStats || !atkPos)
            continue;

        float cd = atkStats->cooldown_remaining - dt;
        if (auto it = new_cooldowns.find(attacker.id());
            it != new_cooldowns.end())
            cd = it->second;

        if (cd > 0.f) {
            new_cooldowns[attacker.id()] = cd;
            continue;
        }

        EntityId targetId = find_nearest_enemy(world, attacker.id(),
                                               atkStats->team, damage_dealt);
        if (targetId == 0)
            continue;

        auto target_e = world.entity(targetId);
        auto const *defStats = target_e.try_get<CombatStats>();
        if (!defStats)
            continue;

        int effHp = defStats->hp - damage_dealt[targetId];
        if (effHp <= 0)
            continue;

        int dmg = calc_damage(atkStats->attack, defStats->defense);
        new_cooldowns[attacker.id()] = atkStats->attack_cooldown;

        // Ranged: spawn projectile instead of instant damage
        auto const *ai = attacker.try_get<SoldierAI>();
        if (ai && ai->role == SoldierRole::ranged) {
            auto const *defPos = target_e.try_get<Position>();
            if (defPos) {
                Projectile p;
                p.pos = atkPos->world_pos;
                p.target_id = targetId;
                p.damage = dmg;
                Vec2f d = defPos->world_pos - p.pos;
                p.total_dist = std::sqrt(d.x * d.x + d.y * d.y);
                p.direction = p.total_dist > 0.f ? Vec2f{d.x / p.total_dist,
                                                         d.y / p.total_dist}
                                                 : Vec2f{1.f, 0.f};
                // Accuracy: farther = less accurate
                float hit_chance =
                    std::clamp(0.9f - p.total_dist * 0.002f, 0.35f, 0.95f);
                if ((float)std::rand() / RAND_MAX > hit_chance)
                    p.damage = 0; // miss
                out_projectiles.push_back(p);
                continue;
            }
        }

        // Melee: instant damage
        damage_dealt[targetId] += dmg;
        out_events.push_back({attacker.id(), targetId, dmg, effHp - dmg <= 0});
    }

    for (auto &[eid, cd] : new_cooldowns) {
        auto *cs = world.entity(eid).try_get_mut<CombatStats>();
        if (cs) {
            cs->cooldown_remaining = cd;
            mark_dirty(eid);
        }
    }
    for (auto &[eid, dmg] : damage_dealt) {
        auto *cs = world.entity(eid).try_get_mut<CombatStats>();
        if (cs) {
            cs->hp -= dmg;
            if (cs->hp <= 0) {
                cs->alive = false;
                cs->hp = 0;
            }
            mark_dirty(eid);
        }
    }
}

inline void update_projectiles(flecs::world &world,
                               std::vector<Projectile> &projectiles, float dt,
                               std::vector<CombatEvent> &out_events)
{
    for (auto it = projectiles.begin(); it != projectiles.end();) {
        float step = it->speed * dt;
        it->pos = it->pos + it->direction * step;
        it->traveled += step;

        if (it->traveled >= it->total_dist) {
            if (it->damage > 0) {
                auto *cs =
                    world.entity(it->target_id).try_get_mut<CombatStats>();
                if (cs && cs->alive) {
                    cs->hp -= it->damage;
                    bool killed = cs->hp <= 0;
                    if (killed) {
                        cs->alive = false;
                        cs->hp = 0;
                    }
                    out_events.push_back(
                        {0, it->target_id, it->damage, killed});
                }
            }
            it = projectiles.erase(it);
        }
        else {
            ++it;
        }
    }
}
