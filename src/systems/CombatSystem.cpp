#include "systems/CombatSystem.hpp"
#include "entities/EntityManager.hpp"
#include "entities/components/Position.hpp"
#include "entities/components/Sprite.hpp"
#include "entities/components/SoldierAI.hpp"
#include <cmath>
#include <cstdlib>
#include <algorithm>

void CombatSystem::update(EntityManager& entities, float dt) {
    resolveCombat(entities, dt);
    processSoldierAI(entities);
}

void CombatSystem::resolveCombat(EntityManager& entities, float dt) {
    // Collect all combat-capable entities
    std::vector<EntityId> combatants;
    for (auto id : entities.allEntities()) {
        auto* cs = entities.getComponent<CombatStats>(id);
        if (cs && cs->alive && cs->team != Team::Neutral) {
            combatants.push_back(id);
        }
    }

    for (auto attackerId : combatants) {
        auto* atkStats = entities.getComponent<CombatStats>(attackerId);
        if (!atkStats || !atkStats->alive) continue;

        atkStats->cooldownRemaining -= dt;
        if (atkStats->cooldownRemaining > 0.f) continue;

        // Find nearest enemy
        Team enemyTeam = (atkStats->team == Team::Player) ? Team::Enemy : Team::Player;
        auto targetId = findNearestEnemy(entities, attackerId, enemyTeam);
        if (targetId == 0) continue;

        auto* defStats = entities.getComponent<CombatStats>(targetId);
        if (!defStats || !defStats->alive) continue;

        // Attack!
        int dmg = calcDamage(atkStats->attack, defStats->defense);
        defStats->hp -= dmg;
        atkStats->cooldownRemaining = atkStats->attackCooldown;

        bool killed = defStats->hp <= 0;
        if (killed) {
            defStats->alive = false;
            defStats->hp = 0;
        }

        m_events.push_back({attackerId, targetId, "attacker", "defender", dmg, killed});
    }
}

void CombatSystem::processSoldierAI(EntityManager& entities) {
    const float SOLDIER_SPEED = 120.f;

    for (auto id : entities.allEntities()) {
        auto* ai = entities.getComponent<SoldierAI>(id);
        if (!ai) continue;

        auto* pos = entities.getComponent<Position>(id);
        auto* cs = entities.getComponent<CombatStats>(id);
        if (!pos || !cs || !cs->alive) continue;

        // Check for nearby enemies
        auto enemyId = findNearestEnemy(entities, id,
            cs->team == Team::Player ? Team::Enemy : Team::Player);
        auto* enemyPos = entities.getComponent<Position>(enemyId);

        if (enemyId != 0 && enemyPos) {
            Vec2f diff = enemyPos->worldPos - pos->worldPos;
            float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);

            if (dist <= ai->engageRange) {
                // Move toward enemy
                if (dist > cs->attackRange) {
                    Vec2f dir = {diff.x / dist, diff.y / dist};
                    pos->worldPos.x += dir.x * SOLDIER_SPEED * 1.2f / 60.f;
                    pos->worldPos.y += dir.y * SOLDIER_SPEED * 1.2f / 60.f;
                }
                ai->inCombat = true;
                continue;
            }
        }

        ai->inCombat = false;

        // Follow leader
        auto* leaderPos = entities.getComponent<Position>(ai->followTarget);
        if (!leaderPos) continue;

        Vec2f goal = leaderPos->worldPos + ai->formationOffset;
        Vec2f diff = goal - pos->worldPos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);

        if (dist > ai->followDistance) {
            Vec2f dir = diff.x == 0 && diff.y == 0
                ? Vec2f{1.f, 0.f} : Vec2f{diff.x / dist, diff.y / dist};
            pos->worldPos.x += dir.x * SOLDIER_SPEED / 60.f;
            pos->worldPos.y += dir.y * SOLDIER_SPEED / 60.f;
        }
        pos->tilePos = {(int)(pos->worldPos.x / 64.f), (int)(pos->worldPos.y / 64.f)};
    }
}

EntityId CombatSystem::findNearestEnemy(EntityManager& entities,
                                         EntityId self, Team enemyTeam) const {
    auto* selfPos = entities.getComponent<Position>(self);
    auto* selfStats = entities.getComponent<CombatStats>(self);
    if (!selfPos || !selfStats) return 0;

    EntityId nearest = 0;
    float nearestDist = selfStats->attackRange;

    for (auto id : entities.allEntities()) {
        if (id == self) continue;
        auto* cs = entities.getComponent<CombatStats>(id);
        if (!cs || !cs->alive || cs->team != enemyTeam) continue;

        auto* pos = entities.getComponent<Position>(id);
        if (!pos) continue;

        Vec2f diff = pos->worldPos - selfPos->worldPos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = id;
        }
    }
    return nearest;
}

int CombatSystem::calcDamage(int attack, int defense) const {
    int base = std::max(1, attack - defense / 2);
    int variance = std::rand() % 3 - 1;  // -1, 0, or +1
    return std::max(1, base + variance);
}

bool CombatSystem::teamNearPosition(EntityManager& entities, Team team,
                                     Vec2f pos, float radius) const {
    for (auto id : entities.allEntities()) {
        auto* cs = entities.getComponent<CombatStats>(id);
        if (!cs || !cs->alive || cs->team != team) continue;
        auto* p = entities.getComponent<Position>(id);
        if (!p) continue;
        Vec2f d = p->worldPos - pos;
        if (std::sqrt(d.x * d.x + d.y * d.y) <= radius) return true;
    }
    return false;
}

void CombatSystem::spawnEnemyWave(EntityManager& entities, int count,
                                   Vec2f center, float spread, Team team) {
    for (int i = 0; i < count; ++i) {
        auto eid = entities.createEntity();
        float ang = (float)(std::rand() % 360) * 3.14159f / 180.f;
        float dist = (float)(std::rand() % (int)spread);
        float x = center.x + std::cos(ang) * dist;
        float y = center.y + std::sin(ang) * dist;

        entities.addComponent<Position>(eid, Position{
            .worldPos = {x, y},
            .tilePos = {(int)(x / 64.f), (int)(y / 64.f)},
            .zOrder = 0.5f
        });
        entities.addComponent<Sprite>(eid, Sprite{
            .origin = {12.f, 12.f},
            .color = {0.9f, 0.2f, 0.1f, 1.f},
            .scale = 1.f, .visible = true
        });
        entities.addComponent<CombatStats>(eid, CombatStats{
            .team = team, .maxHp = 8, .hp = 8,
            .attack = 3, .defense = 1, .attackRange = 80.f
        });
    }
}
