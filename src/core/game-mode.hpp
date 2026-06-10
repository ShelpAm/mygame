#pragma once

#include "core/game-types.hpp"
#include "entities/components/position.hpp"
#include "entities/components/combat-stats.hpp"
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
    bool is_client = false;  // Set true when this is a client-side GameMode
    void init_world(WorldState& ws, KnowledgeGraph& kg, DialogueEngine& de,
                   TopicRegistry& tr, RelationshipTable& rt, FactionNetwork& fn,
                   EventSimulator& es, RumorPropagator& rp, CombatSystem& cs,
                   QuestManager& qm, NetworkManager& net);

    EntityId spawn_player(float x, float y);
    void spawn_npc(const std::string& id, const std::string& name,
                  float x, float y, const std::string& personality,
                  const std::vector<NPCKnowledgeEntry>& known_facts);
    EntityId spawn_soldier(EntityId leader, int index, const Vec2f& facing);
    void spawn_guards(EntityId captain_eid, int count, Team team);

    void update(float dt, InputManager& input, LocaleManager& loc, WorldState& ws);
    void handle_interaction(LocaleManager& loc);
    void do_dialogue_action(const std::string& action, LocaleManager& loc);
    void end_dialogue();

    DialogueState& dialogue() { return *dialogue_; }
    const DialogueState& dialogue() const { return *dialogue_; }
    EntityId player_entity() const { return player_entity_; }
    const std::vector<EntityId>& npc_entities() const { return npc_entities_; }

    std::unordered_map<int, EntityId> remote_id_map;

private:
    EntityManager& em_;
    EntityManager* em_ptr_ = nullptr;  // Allow switching EntityManager
    EntityId player_entity_ = invalid_entity;
    std::vector<EntityId> npc_entities_;
    std::unique_ptr<DialogueState> dialogue_;

    KnowledgeGraph* kg_ = nullptr;
    DialogueEngine* de_ = nullptr;
    TopicRegistry* tr_ = nullptr;
    RelationshipTable* rt_ = nullptr;
    CombatSystem* cs_ = nullptr;
    QuestManager* qm_ = nullptr;
    WorldState* ws_ = nullptr;

    EntityId find_nearest_interactable(Vec2f player_pos) const;
};
