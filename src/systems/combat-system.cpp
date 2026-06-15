#include "systems/combat-system.hpp"
#include "entities/components/collider.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/sprite.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

void CombatSystem::update(flecs::world &world, float dt)
{
    resolve_combat(world, dt);
    process_soldier_ai(world);
}

void CombatSystem::resolve_combat(flecs::world &world, float dt)
{
    // Collect combatants: entities with CombatStats that are alive and
    // non-neutral
    std::vector<flecs::entity> combatants;
    world.query<CombatStats, Position>().each(
        [&](flecs::entity e, CombatStats &cs, Position &) {
            if (cs.alive && cs.team != Team::neutral)
                combatants.push_back(e);
        });

    // Track accumulated damage and cooldowns locally
    std::unordered_map<EntityId, int> damage_dealt;
    std::unordered_map<EntityId, float> new_cooldowns;

    for (auto &attacker : combatants) {
        auto const *atkStats = attacker.try_get<CombatStats>();
        auto const *atkPos = attacker.try_get<Position>();
        if (!atkStats || !atkPos)
            continue;

        // Apply cooldown reduction
        float cd = atkStats->cooldown_remaining - dt;
        // Check if we already set cooldown for this attacker from a prior
        // iteration (shouldn't happen since we iterate once, but safe)
        if (auto it = new_cooldowns.find(attacker.id());
            it != new_cooldowns.end())
            cd = it->second;

        if (cd > 0.f) {
            new_cooldowns[attacker.id()] = cd;
            continue;
        }

        // Find nearest hostile entity
        EntityId targetId = find_nearest_enemy(world, attacker.id(),
                                               atkStats->team, damage_dealt);
        if (targetId == 0)
            continue;

        auto const *defStats = world.entity(targetId).try_get<CombatStats>();
        if (!defStats)
            continue;

        // Compute defender's current effective HP (original - accumulated
        // damage)
        int effHp = defStats->hp - damage_dealt[targetId];
        if (effHp <= 0)
            continue; // already dead from accumulated damage

        int dmg = calc_damage(atkStats->attack, defStats->defense);
        damage_dealt[targetId] += dmg;

        // Reset attacker cooldown
        new_cooldowns[attacker.id()] = atkStats->attack_cooldown;

        events_.push_back({attacker.id(), targetId, dmg, effHp - dmg <= 0});
    }

    // Write back all modifications
    for (auto &[eid, cd] : new_cooldowns) {
        auto *cs = world.entity(eid).try_get_mut<CombatStats>();
        if (cs) {
            cs->cooldown_remaining = cd;
            if (dirty_cb_)
                dirty_cb_(eid);
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
            if (dirty_cb_)
                dirty_cb_(eid);
        }
    }
}

void CombatSystem::process_soldier_ai(flecs::world &world)
{
    float const SOLDIER_SPEED = 120.f;

    std::vector<flecs::entity> soldiers;
    world.query<SoldierAI, Position, CombatStats>().each(
        [&](flecs::entity e, SoldierAI &, Position &, CombatStats &cs) {
            if (cs.alive)
                soldiers.push_back(e);
        });

    for (auto &e : soldiers) {
        auto *ai = e.try_get_mut<SoldierAI>();
        auto *pos = e.try_get_mut<Position>();
        auto const *cs = e.try_get<CombatStats>();
        if (!ai || !pos || !cs)
            continue;

        bool changed = false;
        // Find nearest hostile entity
        auto enemyId = find_nearest_enemy(world, e.id(), cs->team, {});
        if (enemyId != 0) {
            auto const *enemyPos = world.entity(enemyId).try_get<Position>();
            assert(enemyPos);
            Vec2f diff = enemyPos->world_pos - pos->world_pos;
            float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);

            if (dist <= ai->engage_range) {
                if (dist > cs->attack_range) {
                    Vec2f dir = {diff.x / dist, diff.y / dist};
                    pos->world_pos.x += dir.x * SOLDIER_SPEED * 1.2f / 60.f;
                    pos->world_pos.y += dir.y * SOLDIER_SPEED * 1.2f / 60.f;
                    changed = true;
                }
                if (!ai->in_combat) {
                    ai->in_combat = true;
                    changed = true;
                }
                pos->tile_pos = {(int)(pos->world_pos.x / 64.f),
                                 (int)(pos->world_pos.y / 64.f)};
                if (dirty_cb_ && changed)
                    dirty_cb_(e.id());
                continue;
            }
        }

        if (ai->in_combat) {
            ai->in_combat = false;
            changed = true;
        }

        auto const *leaderPos =
            world.entity(ai->follow_target).try_get<Position>();
        if (!leaderPos) {
            if (dirty_cb_ && changed)
                dirty_cb_(e.id());
            continue;
        }

        Vec2f goal = leaderPos->world_pos + ai->formation_offset;
        Vec2f diff = goal - pos->world_pos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);

        if (dist > ai->follow_distance) {
            Vec2f dir = diff.x == 0 && diff.y == 0
                            ? Vec2f{1.f, 0.f}
                            : Vec2f{diff.x / dist, diff.y / dist};
            pos->world_pos.x += dir.x * SOLDIER_SPEED / 60.f;
            pos->world_pos.y += dir.y * SOLDIER_SPEED / 60.f;
            changed = true;
        }
        pos->tile_pos = {(int)(pos->world_pos.x / 64.f),
                         (int)(pos->world_pos.y / 64.f)};
        if (dirty_cb_ && changed)
            dirty_cb_(e.id());
    }
}

EntityId CombatSystem::find_nearest_enemy(
    flecs::world &world, EntityId self, Team my_team,
    std::unordered_map<EntityId, int> const &extra_damage) const
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
            // Skip entities that would be dead from accumulated damage
            auto it = extra_damage.find(e.id());
            if (it != extra_damage.end() && cs.hp - it->second <= 0)
                return;
            float d = std::sqrt((pos.world_pos.x - selfPos->world_pos.x) *
                                    (pos.world_pos.x - selfPos->world_pos.x) +
                                (pos.world_pos.y - selfPos->world_pos.y) *
                                    (pos.world_pos.y - selfPos->world_pos.y));
            if (d < nearestDist) {
                nearestDist = d;
                nearest = e.id();
            }
        });
    return nearest;
}

int CombatSystem::calc_damage(int attack, int defense) const
{
    int base = std::max(1, attack - defense / 2);
    int variance = std::rand() % 3 - 1;
    return std::max(1, base + variance);
}

bool CombatSystem::team_near_position(flecs::world &world, Team team, Vec2f pos,
                                      float radius) const
{
    bool found = false;
    world.query<CombatStats, Position>().each(
        [&](flecs::entity, CombatStats &cs, Position &p) {
            if (!cs.alive || cs.team != team)
                return;
            Vec2f d = p.world_pos - pos;
            if (std::sqrt(d.x * d.x + d.y * d.y) <= radius)
                found = true;
        });
    return found;
}

void CombatSystem::spawn_enemy_wave(flecs::world &world, int count,
                                    Vec2f center, float spread, Team team,
                                    std::vector<EntityId> *out_ids)
{
    for (int i = 0; i < count; ++i) {
        auto e = world.entity();
        float ang = (float)(std::rand() % 360) * 3.14159f / 180.f;
        float dist = (float)(std::rand() % (int)spread);
        float x = center.x + std::cos(ang) * dist;
        float y = center.y + std::sin(ang) * dist;

        e.set<Position>(
            Position{{x, y}, {(int)(x / 64.f), (int)(y / 64.f)}, 0.5f});
        e.set<Sprite>(Sprite{.origin = {12.f, 12.f},
                             .color = {0.9f, 0.2f, 0.1f, 1.f},
                             .scale = 1.f,
                             .visible = true});
        e.set<CombatStats>(CombatStats{team, 8, 8, 3, 1, 80.f});
        e.set<Collider>(Collider{14.f});
        if (out_ids)
            out_ids->push_back(e.id());
    }
}
