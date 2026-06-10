#pragma once

#include "entities/EntityManager.hpp"
#include <unordered_map>

class NetworkManager;
class CombatSystem;
class WorldState;
class QuestManager;

class Client {
public:
    Client();
    void setManagers(CombatSystem* cs, WorldState* ws, QuestManager* qm);

    void update(float dt, NetworkManager& net);
    void handleCombatEvent(int attackerId, int defenderId, int damage, bool killed);

    EntityManager& entities() { return m_em; }
    EntityId localPlayer() const;
    Vec2f localPlayerPos();
    void setLocalPlayerPos(Vec2f pos);
    const EntityManager& entities() const { return m_em; }

private:
    EntityManager m_em;
    EntityId m_myPlayer = 0;
    std::unordered_map<int, EntityId> m_idMap;

    void applySync(const std::vector<uint8_t>& data);
};
