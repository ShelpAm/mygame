#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "entities/components/combat-stats.hpp"
#include "save/save-manager.hpp"
#include <flecs.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class KnowledgeGraph;
class DialogueEngine;
class FactionNetwork;
class EventSimulator;
class RumorPropagator;
class CombatSystem;
class QuestManager;
class WorldState;
class ConditionTracker;
struct NPCState;
#include "entities/components/position.hpp"

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
    EntityId spawn_soldier(EntityId leader, int index, Vec2f const &facing,
                           Vec2f extra_offset = {});
    void spawn_guards(EntityId captain_eid, int count, Team team);

    void update(float dt);
    void apply_player_movement(EntityId player, Vec2f new_pos);
    void handle_interaction(EntityId player);
    void do_dialogue_action(std::string const &action);
    void end_dialogue();

    // ECS mutators (used by Server instead of direct world access)
    void apply_player_input(EntityId entity, Vec2f dir);
    void spawn_enemy_wave(int count, Vec2f center, float spread, Team team);
    void apply_damage(EntityId target, int damage, bool killed);
    void heal_entity(EntityId entity, int amount);
    void sync_entity_state(EntityId entity, Vec2f pos, int hp, int max_hp,
                           bool alive);
    void set_entity_position(EntityId entity, Vec2f pos);
    const Position *get_position(EntityId entity) const;

    // Dirty tracking + network sync payloads
    bool has_dirty_entities() const;
    std::vector<uint8_t> build_dirty_payload();
    std::vector<uint8_t> build_full_payload() const;
    void mark_frame_clean();

    void set_survival(ConditionTracker *s)
    {
        survival_ = s;
    }
    void set_event_simulator(EventSimulator *ev)
    {
        events_ = ev;
    }
    void register_player(EntityId pid)
    {
        player_entities_.insert(pid);
    }
    EntityId spawn_player_for_server(Vec2f pos);

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
  private:
    flecs::world &world()
    {
        return world_;
    }
    flecs::world const &world() const
    {
        return world_;
    }
    flecs::world world_;

    std::vector<EntityId> npc_entities_;
    std::unique_ptr<DialogueState> dialogue_ =
        std::make_unique<DialogueState>();
    TopicRegistry topic_registry_;
    RelationshipTable relationships_;

    KnowledgeGraph *kg_ = nullptr;
    DialogueEngine *de_ = nullptr;
    CombatSystem *cs_ = nullptr;
    QuestManager *qm_ = nullptr;
    WorldState *ws_ = nullptr;
    EventSimulator *events_ = nullptr;
    ConditionTracker *survival_ = nullptr;

    std::unordered_set<EntityId> player_entities_;
    std::unordered_set<EntityId> dirty_entities_;
    size_t last_event_count_ = 0;

    void mark_dirty(EntityId eid)
    {
        dirty_entities_.insert(eid);
    }
    EntityId find_nearest_interactable(EntityId player, Vec2f player_pos);
};
