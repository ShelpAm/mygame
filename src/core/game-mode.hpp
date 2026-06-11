#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/entity-manager.hpp"
#include "save/save-manager.hpp"
#include <memory>
#include <string>
#include <vector>
class KnowledgeGraph;
class DialogueEngine;
class FactionNetwork;
class EventSimulator;
class RumorPropagator;
class CombatSystem;
class QuestManager;
class WorldState;
struct NPCState;

class GameMode {
  public:
    GameMode();
    void init_world(WorldState &ws, KnowledgeGraph &kg, DialogueEngine &de,
                    FactionNetwork &fn, EventSimulator &es, RumorPropagator &rp,
                    CombatSystem &cs, QuestManager &qm);

    EntityId spawn_player(float x, float y);
    void spawn_npc(std::string const &id, std::string const &name, float x,
                   float y, std::string const &personality,
                   std::vector<NPCKnowledgeEntry> const &known_facts);
    EntityId spawn_soldier(EntityId leader, int index, Vec2f const &facing);
    void spawn_guards(EntityId captain_eid, int count, Team team);

    void update(EntityId player, float dt);
    void apply_player_movement(EntityId player, Vec2f new_pos);
    void handle_interaction(EntityId player);
    void do_dialogue_action(std::string const &action);
    void end_dialogue();

    EntityId load_world(SaveManager::SaveData const &data);
    std::vector<SaveManager::NPCData> collect_npc_save_data();

    DialogueState &dialogue()
    {
        return *dialogue_;
    }
    DialogueState const &dialogue() const
    {
        return *dialogue_;
    }
    std::vector<EntityId> const &npc_entities() const
    {
        return npc_entities_;
    }
    void clear_npc_list()
    {
        npc_entities_.clear();
    }
    EntityManager &entities()
    {
        return em_;
    }
    EntityManager const &entities() const
    {
        return em_;
    }

  private:
    EntityManager em_;
    std::vector<EntityId> npc_entities_;
    std::unique_ptr<DialogueState> dialogue_ = std::make_unique<DialogueState>();
    TopicRegistry topic_registry_;
    RelationshipTable relationships_;

    KnowledgeGraph *kg_ = nullptr;
    DialogueEngine *de_ = nullptr;
    CombatSystem *cs_ = nullptr;
    QuestManager *qm_ = nullptr;
    WorldState *ws_ = nullptr;

    EntityId find_nearest_interactable(EntityId player, Vec2f player_pos);
};
