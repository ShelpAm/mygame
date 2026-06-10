#pragma once

#include "core/GameTypes.hpp"
#include "entities/components/Position.hpp"
#include "entities/components/CombatStats.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

class EntityManager;
class KnowledgeGraph;
class DialogueEngine;
class TopicRegistry;
class RelationshipTable;
class FactionNetwork;
class EventSimulator;
class RumorPropagator;
class CombatSystem;
class QuestManager;
class NetworkManager;
class WorldState;
class LocaleManager;
class InputManager;
struct NPCState;

class GameMode {
public:
    explicit GameMode(EntityManager& em);
    void setEntityManager(EntityManager& em) { m_emPtr = &em; }
    void initWorld(WorldState& ws, KnowledgeGraph& kg, DialogueEngine& de,
                   TopicRegistry& tr, RelationshipTable& rt, FactionNetwork& fn,
                   EventSimulator& es, RumorPropagator& rp, CombatSystem& cs,
                   QuestManager& qm, NetworkManager& net);

    EntityId spawnPlayer(float x, float y);
    void spawnNPC(const std::string& id, const std::string& name,
                  float x, float y, const std::string& personality,
                  const std::vector<NPCKnowledgeEntry>& knownFacts);
    EntityId spawnSoldier(EntityId leader, int index, const Vec2f& facing);
    void spawnGuards(EntityId captainEid, int count, Team team);

    void update(float dt, InputManager& input, LocaleManager& loc, WorldState& ws);
    void handleInteraction(LocaleManager& loc);
    void doDialogueAction(const std::string& action, LocaleManager& loc);
    void endDialogue();

    DialogueState& dialogue() { return *m_dialogue; }
    const DialogueState& dialogue() const { return *m_dialogue; }
    EntityId playerEntity() const { return m_playerEntity; }
    const std::vector<EntityId>& npcEntities() const { return m_npcEntities; }

    std::unordered_map<int, EntityId> remoteIdMap;

private:
    EntityManager& m_em;
    EntityManager* m_emPtr = nullptr;  // Allow switching EntityManager
    EntityId m_playerEntity = INVALID_ENTITY;
    std::vector<EntityId> m_npcEntities;
    std::unique_ptr<DialogueState> m_dialogue;

    KnowledgeGraph* m_kg = nullptr;
    DialogueEngine* m_de = nullptr;
    TopicRegistry* m_tr = nullptr;
    RelationshipTable* m_rt = nullptr;
    CombatSystem* m_cs = nullptr;
    QuestManager* m_qm = nullptr;
    WorldState* m_ws = nullptr;

    EntityId findNearestInteractable(Vec2f playerPos) const;
};
