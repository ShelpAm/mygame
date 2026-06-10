#include "systems/combat-system.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/sprite.hpp"
#include "entities/entity-manager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

void CombatSystem::update(EntityManager &entities, float dt)
{
    resolve_combat(entities, dt);
    process_soldier_ai(entities);
}

void CombatSystem::resolve_combat(EntityManager &entities, float dt)
{
    // Collect all combat-capable entities
    std::vector<EntityId> combatants;
    for (auto id : entities.all_entities()) {
        auto *cs = entities.get_component<CombatStats>(id);
        if (cs && cs->alive && cs->team != Team::neutral) {
            combatants.push_back(id);
        }
    }

    for (auto attacker_id : combatants) {
        auto *atkStats = entities.get_component<CombatStats>(attacker_id);
        if (!atkStats || !atkStats->alive)
            continue;

        atkStats->cooldown_remaining -= dt;
        if (atkStats->cooldown_remaining > 0.f)
            continue;

        // Find nearest enemy
        Team enemy_team =
            (atkStats->team == Team::player) ? Team::enemy : Team::player;
        auto targetId = find_nearest_enemy(entities, attacker_id, enemy_team);
        if (targetId == 0)
            continue;

        auto *defStats = entities.get_component<CombatStats>(targetId);
        if (!defStats || !defStats->alive)
            continue;

        // Attack!
        int dmg = calc_damage(atkStats->attack, defStats->defense);
        defStats->hp -= dmg;
        atkStats->cooldown_remaining = atkStats->attack_cooldown;

        bool killed = defStats->hp <= 0;
        if (killed) {
            defStats->alive = false;
            defStats->hp = 0;
        }

        events_.push_back(
            {attacker_id, targetId, "attacker", "defender", dmg, killed});
    }
}

void CombatSystem::process_soldier_ai(EntityManager &entities)
{
    float const SOLDIER_SPEED = 120.f;

    for (auto id : entities.all_entities()) {
        auto *ai = entities.get_component<SoldierAI>(id);
        if (!ai)
            continue;

        auto *pos = entities.get_component<Position>(id);
        auto *cs = entities.get_component<CombatStats>(id);
        if (!pos || !cs || !cs->alive)
            continue;

        // Check for nearby enemies
        auto enemyId = find_nearest_enemy(
            entities, id,
            cs->team == Team::player ? Team::enemy : Team::player);
        auto *enemyPos = entities.get_component<Position>(enemyId);

        if (enemyId != 0 && enemyPos) {
            Vec2f diff = enemyPos->world_pos - pos->world_pos;
            float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);

            if (dist <= ai->engage_range) {
                // Move toward enemy
                if (dist > cs->attack_range) {
                    Vec2f dir = {diff.x / dist, diff.y / dist};
                    pos->world_pos.x += dir.x * SOLDIER_SPEED * 1.2f / 60.f;
                    pos->world_pos.y += dir.y * SOLDIER_SPEED * 1.2f / 60.f;
                }
                ai->in_combat = true;
                continue;
            }
        }

        ai->in_combat = false;

        // Follow leader
        auto *leaderPos = entities.get_component<Position>(ai->follow_target);
        if (!leaderPos)
            continue;

        Vec2f goal = leaderPos->world_pos + ai->formation_offset;
        Vec2f diff = goal - pos->world_pos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);

        if (dist > ai->follow_distance) {
            Vec2f dir = diff.x == 0 && diff.y == 0
                            ? Vec2f{1.f, 0.f}
                            : Vec2f{diff.x / dist, diff.y / dist};
            pos->world_pos.x += dir.x * SOLDIER_SPEED / 60.f;
            pos->world_pos.y += dir.y * SOLDIER_SPEED / 60.f;
        }
        pos->tile_pos = {(int)(pos->world_pos.x / 64.f),
                         (int)(pos->world_pos.y / 64.f)};
    }
}

EntityId CombatSystem::find_nearest_enemy(EntityManager &entities,
                                          EntityId self, Team enemy_team) const
{
    auto *selfPos = entities.get_component<Position>(self);
    auto *selfStats = entities.get_component<CombatStats>(self);
    if (!selfPos || !selfStats)
        return 0;

    EntityId nearest = 0;
    float nearestDist = selfStats->attack_range;

    for (auto id : entities.all_entities()) {
        if (id == self)
            continue;
        auto *cs = entities.get_component<CombatStats>(id);
        if (!cs || !cs->alive || cs->team != enemy_team)
            continue;

        auto *pos = entities.get_component<Position>(id);
        if (!pos)
            continue;

        Vec2f diff = pos->world_pos - selfPos->world_pos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = id;
        }
    }
    return nearest;
}

int CombatSystem::calc_damage(int attack, int defense) const
{
    int base = std::max(1, attack - defense / 2);
    int variance = std::rand() % 3 - 1; // -1, 0, or +1
    return std::max(1, base + variance);
}

bool CombatSystem::team_near_position(EntityManager &entities, Team team,
                                      Vec2f pos, float radius) const
{
    for (auto id : entities.all_entities()) {
        auto *cs = entities.get_component<CombatStats>(id);
        if (!cs || !cs->alive || cs->team != team)
            continue;
        auto *p = entities.get_component<Position>(id);
        if (!p)
            continue;
        Vec2f d = p->world_pos - pos;
        if (std::sqrt(d.x * d.x + d.y * d.y) <= radius)
            return true;
    }
    return false;
}

void CombatSystem::spawn_enemy_wave(EntityManager &entities, int count,
                                    Vec2f center, float spread, Team team)
{
    for (int i = 0; i < count; ++i) {
        auto eid = entities.create_entity();
        float ang = (float)(std::rand() % 360) * 3.14159f / 180.f;
        float dist = (float)(std::rand() % (int)spread);
        float x = center.x + std::cos(ang) * dist;
        float y = center.y + std::sin(ang) * dist;

        entities.add_component<Position>(
            eid, Position{.world_pos = {x, y},
                          .tile_pos = {(int)(x / 64.f), (int)(y / 64.f)},
                          .z_order = 0.5f});
        entities.add_component<Sprite>(eid,
                                       Sprite{.origin = {12.f, 12.f},
                                              .color = {0.9f, 0.2f, 0.1f, 1.f},
                                              .scale = 1.f,
                                              .visible = true});
        entities.add_component<CombatStats>(eid,
                                            CombatStats{.team = team,
                                                        .max_hp = 8,
                                                        .hp = 8,
                                                        .attack = 3,
                                                        .defense = 1,
                                                        .attack_range = 80.f});
    }
}
