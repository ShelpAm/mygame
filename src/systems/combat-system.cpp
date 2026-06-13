#include "systems/combat-system.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/sprite.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

void CombatSystem::update(flecs::world &world, float dt)
{
    resolve_combat(world, dt);
    process_soldier_ai(world);
}

struct EnemyInfo {
    EntityId id;
    Vec2f world_pos;
    CombatStats stats;
};

void CombatSystem::resolve_combat(flecs::world &world, float dt)
{
    // Phase 1: collect all data from queries
    struct Combatant {
        EntityId id;
        CombatStats stats;
        Vec2f world_pos;
    };
    std::vector<Combatant> combatants;
    std::vector<EnemyInfo> all_enemies;

    world.query<CombatStats, Position>().each(
        [&](flecs::entity e, CombatStats &cs, Position &pos) {
            all_enemies.push_back({e.id(), pos.world_pos, cs});
            if (cs.alive && cs.team != Team::neutral)
                combatants.push_back({e.id(), cs, pos.world_pos});
        });

    // Phase 2: process
    struct CombatUpdate {
        EntityId id;
        CombatStats new_stats;
    };
    std::vector<CombatUpdate> updates;

    for (auto &attacker : combatants) {
        attacker.stats.cooldown_remaining -= dt;
        if (attacker.stats.cooldown_remaining > 0.f)
            continue;

        Team enemy_team = (attacker.stats.team == Team::player) ? Team::enemy
                                                                 : Team::player;

        EntityId targetId = 0;
        float nearestDist = attacker.stats.attack_range;
        for (auto &enemy : all_enemies) {
            if (enemy.id == attacker.id || enemy.stats.team != enemy_team ||
                !enemy.stats.alive)
                continue;
            float d = std::hypot(enemy.world_pos.x - attacker.world_pos.x,
                                 enemy.world_pos.y - attacker.world_pos.y);
            if (d < nearestDist) {
                nearestDist = d;
                targetId = enemy.id;
            }
        }

        if (targetId == 0)
            continue;

        // Find defender stats
        int def_defense = 0;
        bool def_killed = false;
        for (auto &target : all_enemies) {
            if (target.id == targetId) {
                def_defense = target.stats.defense;
                int dmg = calc_damage(attacker.stats.attack, def_defense);

                attacker.stats.cooldown_remaining =
                    attacker.stats.attack_cooldown;
                updates.push_back({attacker.id, attacker.stats});

                target.stats.hp -= dmg;
                if (target.stats.hp <= 0) {
                    target.stats.alive = false;
                    target.stats.hp = 0;
                    def_killed = true;
                }
                updates.push_back({targetId, target.stats});

                events_.push_back({attacker.id, targetId, "attacker",
                                   "defender", dmg, def_killed});
                break;
            }
        }
    }

    // Phase 3: write back all modifications
    for (auto &up : updates)
        world.entity(up.id).set<CombatStats>(up.new_stats);
}

void CombatSystem::process_soldier_ai(flecs::world &world)
{
    float const SOLDIER_SPEED = 120.f;

    // Collect soldiers and enemies data in a single pass
    struct SoldierData {
        EntityId id;
        SoldierAI ai;
        Position pos;
        CombatStats cs;
    };
    std::vector<SoldierData> soldiers;
    std::vector<EnemyInfo> all_enemies;

    world.query<SoldierAI, Position, CombatStats>().each(
        [&](flecs::entity e, SoldierAI &ai, Position &pos, CombatStats &cs) {
            all_enemies.push_back({e.id(), pos.world_pos, cs});
            if (cs.alive)
                soldiers.push_back({e.id(), ai, pos, cs});
        });

    // Process and write back using set<Position>/set<SoldierAI>
    for (auto &s : soldiers) {
        // Check for nearby enemies
        Team enemy_team =
            s.cs.team == Team::player ? Team::enemy : Team::player;
        EntityId enemyId = 0;
        Vec2f enemyWorldPos;
        float nearestDist = s.ai.engage_range;
        for (auto &enemy : all_enemies) {
            if (enemy.id == s.id || enemy.stats.team != enemy_team ||
                !enemy.stats.alive)
                continue;
            float d = std::hypot(enemy.world_pos.x - s.pos.world_pos.x,
                                 enemy.world_pos.y - s.pos.world_pos.y);
            if (d < nearestDist) {
                nearestDist = d;
                enemyId = enemy.id;
                enemyWorldPos = enemy.world_pos;
            }
        }

        if (enemyId != 0) {
            Vec2f diff = enemyWorldPos - s.pos.world_pos;
            float dist = std::hypot(diff.x, diff.y);

            if (dist <= s.ai.engage_range) {
                if (dist > s.cs.attack_range) {
                    Vec2f dir = {diff.x / dist, diff.y / dist};
                    s.pos.world_pos.x +=
                        dir.x * SOLDIER_SPEED * 1.2f / 60.f;
                    s.pos.world_pos.y +=
                        dir.y * SOLDIER_SPEED * 1.2f / 60.f;
                }
                s.ai.in_combat = true;
                s.pos.tile_pos = {(int)(s.pos.world_pos.x / 64.f),
                                  (int)(s.pos.world_pos.y / 64.f)};
                world.entity(s.id).set<Position>(s.pos);
                world.entity(s.id).set<SoldierAI>(s.ai);
                continue;
            }
        }

        s.ai.in_combat = false;

        // Follow leader
        const auto *leaderPos = world.try_get<Position>(s.ai.follow_target);
        if (!leaderPos)
            continue;

        Vec2f goal = leaderPos->world_pos + s.ai.formation_offset;
        Vec2f diff = goal - s.pos.world_pos;
        float dist = std::hypot(diff.x, diff.y);

        if (dist > s.ai.follow_distance) {
            Vec2f dir = diff.x == 0 && diff.y == 0
                            ? Vec2f{1.f, 0.f}
                            : Vec2f{diff.x / dist, diff.y / dist};
            s.pos.world_pos.x += dir.x * SOLDIER_SPEED / 60.f;
            s.pos.world_pos.y += dir.y * SOLDIER_SPEED / 60.f;
        }
        s.pos.tile_pos = {(int)(s.pos.world_pos.x / 64.f),
                          (int)(s.pos.world_pos.y / 64.f)};

        world.entity(s.id).set<Position>(s.pos);
        world.entity(s.id).set<SoldierAI>(s.ai);
    }
}

EntityId CombatSystem::find_nearest_enemy(flecs::world &world, EntityId self,
                                          Team enemy_team) const
{
    const auto *selfPos = world.try_get<Position>(self);
    const auto *selfStats = world.try_get<CombatStats>(self);
    if (!selfPos || !selfStats)
        return 0;

    EntityId nearest = 0;
    float nearestDist = selfStats->attack_range;

    world.query<CombatStats, Position>().each(
        [&](flecs::entity e, CombatStats &cs, Position &pos) {
            if (e.id() == self || !cs.alive || cs.team != enemy_team)
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

int CombatSystem::calc_damage(int attack, int defense) const
{
    int base = std::max(1, attack - defense / 2);
    int variance = std::rand() % 3 - 1;
    return std::max(1, base + variance);
}

bool CombatSystem::team_near_position(flecs::world &world, Team team,
                                      Vec2f pos, float radius) const
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
                                    Vec2f center, float spread, Team team)
{
    for (int i = 0; i < count; ++i) {
        auto e = world.entity();
        float ang = (float)(std::rand() % 360) * 3.14159f / 180.f;
        float dist = (float)(std::rand() % (int)spread);
        float x = center.x + std::cos(ang) * dist;
        float y = center.y + std::sin(ang) * dist;

        e.set<Position>(Position{{x, y},
                                 {(int)(x / 64.f), (int)(y / 64.f)},
                                 0.5f});
        e.set<Sprite>(
            Sprite{.origin = {12.f, 12.f},
                   .color = {0.9f, 0.2f, 0.1f, 1.f},
                   .scale = 1.f,
                   .visible = true});
        e.set<CombatStats>(CombatStats{team, 8, 8, 3, 1, 80.f});
    }
}
