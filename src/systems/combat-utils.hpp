#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/combat-system.hpp"
#include <cmath>
#include <cstdlib>
#include <flecs.h>
#include <unordered_map>

inline int calc_damage(int attack, int defense)
{
    int base = std::max(1, attack - defense / 2);
    int variance = std::rand() % 3 - 1;
    return std::max(1, base + variance);
}

/// time-complexity: O(n)
inline EntityId
find_nearest_enemy(flecs::world &world, EntityId self, Team my_team,
                   std::unordered_map<EntityId, int> const &extra_damage = {})
{
    flecs::entity self_e = world.entity(self);
    auto const *selfPos = self_e.try_get<Position>();
    auto const *selfStats = self_e.try_get<CombatStats>();
    if (!selfPos || !selfStats)
        return 0;

    EntityId nearest = 0;
    float nearestDist = selfStats->attack_range;

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

// Shared soldier AI — used by SoldierAI system + testable standalone.
template <typename DirtyFn>
inline void run_soldier_ai(flecs::world &world, flecs::entity e, SoldierAI &ai,
                           Position &pos, CombatStats &cs, DirtyFn &&mark_dirty)
{
    if (!cs.alive)
        return;
    float const SOLDIER_SPEED = 120.f;
    bool changed = false;

    auto enemyId = find_nearest_enemy(world, e.id(), cs.team, {});
    if (enemyId != 0) {
        auto const *enemyPos = world.entity(enemyId).try_get<Position>();
        if (enemyPos) {
            Vec2f diff = enemyPos->world_pos - pos.world_pos;
            float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
            if (dist <= ai.engage_range) {
                if (dist > cs.attack_range) {
                    Vec2f dir = {diff.x / dist, diff.y / dist};
                    pos.world_pos.x += dir.x * SOLDIER_SPEED * 1.2f / 60.f;
                    pos.world_pos.y += dir.y * SOLDIER_SPEED * 1.2f / 60.f;
                    changed = true;
                }
                if (!ai.in_combat) {
                    ai.in_combat = true;
                    changed = true;
                }
                pos.tile_pos = {(int)(pos.world_pos.x / 64.f),
                                (int)(pos.world_pos.y / 64.f)};
                if (changed)
                    mark_dirty(e.id());
                return;
            }
        }
    }

    if (ai.in_combat) {
        ai.in_combat = false;
        changed = true;
    }

    auto const *leaderPos = world.entity(ai.follow_target).try_get<Position>();
    if (!leaderPos) {
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
        pos.world_pos.x += dir.x * SOLDIER_SPEED / 60.f;
        pos.world_pos.y += dir.y * SOLDIER_SPEED / 60.f;
        changed = true;
    }
    pos.tile_pos = {(int)(pos.world_pos.x / 64.f),
                    (int)(pos.world_pos.y / 64.f)};
    if (changed)
        mark_dirty(e.id());
}

// Shared combat batch — used by CombatResolution system + testable standalone.
template <typename DirtyFn>
inline void run_combat_batch(flecs::world &world, float dt,
                             std::vector<CombatEvent> &out_events,
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

        auto const *defStats = world.entity(targetId).try_get<CombatStats>();
        if (!defStats)
            continue;

        int effHp = defStats->hp - damage_dealt[targetId];
        if (effHp <= 0)
            continue;

        int dmg = calc_damage(atkStats->attack, defStats->defense);
        damage_dealt[targetId] += dmg;
        new_cooldowns[attacker.id()] = atkStats->attack_cooldown;

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
