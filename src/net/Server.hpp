#pragma once

#include "entities/EntityManager.hpp"
#include "entities/components/Position.hpp"
#include "entities/components/CombatStats.hpp"
#include "core/GameMode.hpp"
#include <memory>
#include <vector>
#include <unordered_map>

class CombatSystem;
class NetworkManager;
class WorldState;
class QuestManager;

class Server {
public:
    Server();
    ~Server();

    void setManagers(CombatSystem* cs, WorldState* ws, QuestManager* qm);
    EntityManager& entities() { return m_em; }
    EntityId spawnPlayer(int id);

    void update(float dt, NetworkManager& net);
    void handleCombatEvent(int attackerId, int defenderId, int damage, bool killed);

    EntityId addEntity(Vec2f pos, Team team, int hp, int maxHp);
    void removeEntity(EntityId id);

    // Remote player management
    bool remotePlayer(int playerId);
    void addRemotePlayer(int playerId, Vec2f pos);
    void updateRemotePlayer(int playerId, Vec2f pos, int hp, int maxHp, bool alive);

private:
    EntityManager m_em;
    std::unordered_map<int, EntityId> m_remotePlayerMap;
    CombatSystem* m_cs = nullptr;
    WorldState* m_ws = nullptr;
    QuestManager* m_qm = nullptr;
    std::vector<EntityId> m_localPlayers;
    float m_syncTimer = 0.f;

    std::vector<uint8_t> buildSyncPayload();
};
