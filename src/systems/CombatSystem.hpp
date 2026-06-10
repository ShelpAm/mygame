#pragma once

#include "entities/components/CombatStats.hpp"
#include "entities/components/Position.hpp"
#include <vector>
#include <string>

class EntityManager;
struct Vec2f;

struct CombatEvent {
    EntityId attackerId = 0;
    EntityId defenderId = 0;
    std::string attackerName;
    std::string defenderName;
    int damage = 0;
    bool killed = false;
};

class CombatSystem {
public:
    void update(EntityManager& entities, float dt);

    // Events generated this frame
    const std::vector<CombatEvent>& events() const { return m_events; }
    void clearEvents() { m_events.clear(); }

    // Check if any entity on a team is near a position
    bool teamNearPosition(EntityManager& entities, Team team,
                          Vec2f pos, float radius) const;

    // Spawn a wave of enemies
    void spawnEnemyWave(EntityManager& entities, int count,
                        Vec2f center, float spread, Team team);

private:
    std::vector<CombatEvent> m_events;

    void resolveCombat(EntityManager& entities, float dt);
    void processSoldierAI(EntityManager& entities);
    EntityId findNearestEnemy(EntityManager& entities,
                               EntityId self, Team enemyTeam) const;
    int calcDamage(int attack, int defense) const;
};
