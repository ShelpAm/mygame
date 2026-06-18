#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/player.hpp"
#include "factions/event-simulator.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "save/save-manager.hpp"
#include "systems/combat-system.hpp"
#include "systems/combat-utils.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"
#include <chrono>
#include <cstdint>
#include <flecs.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Server;
class NavigationSystem;
class CollisionSystem;
struct Formation;

class GameMode {
  public:
    GameMode();
    GameMode(GameMode const &) = delete;
    GameMode(GameMode &&) = delete;
    GameMode &operator=(GameMode const &) = delete;
    GameMode &operator=(GameMode &&) = delete;
    ~GameMode();
    void init_world();
    void load_topics();
    void load_factions();
    void load_events();
    void load_npcs();
    void init_systems();

    void start_host(int port);
    void stop_host();

    void update(float dt);

    EntityId spawn_player(Vec2f pos, Team team);
    EntityId spawn_npc(std::string const &id, std::string const &name, float x,
                       float y, std::string const &personality,
                       std::vector<NPCKnowledgeEntry> const &known_facts);
    EntityId spawn_soldier(EntityId leader, int index, Vec2f const &facing,
                           Vec2f extra_offset = {},
                           SoldierRole role = SoldierRole::melee);
    EntityId spawn_recruit(EntityId leader);
    EntityId spawn_recruit_ranged(EntityId leader);
    void spawn_guards(EntityId captain_eid, int count, Team team);
    void cycle_stance(EntityId leader);
    void cycle_formation(EntityId player, uint8_t role_mask);

    void formation_update(EntityId player, Position &p, Movement &m);
    void handle_interaction(EntityId player);
    void do_dialogue_action(EntityId player, std::string const &action);
    void end_dialogue(EntityId player);

    void apply_player_input(EntityId entity, Vec2f dir);
    void spawn_enemy_wave(int count, Vec2f center, float spread, Team team);
    void apply_damage(EntityId target, int damage, bool killed);
    void heal_entity(EntityId entity, int amount);
    void respawn_player(EntityId pid);

    void sync_entity_state(EntityId entity, Vec2f pos, int hp, int max_hp,
                           bool alive);

    struct PlayerSyncPayload {
        std::vector<uint8_t> bytes;
        std::unordered_set<EntityId> entity_ids;
    };

    bool has_dirty_entities() const;
    PlayerSyncPayload
    build_dirty_payload(EntityId player_eid,
                        std::unordered_set<EntityId> const &prev_sent);
    PlayerSyncPayload build_full_payload(EntityId player_eid) const;
    void mark_frame_clean();

    void recompute_player_visibility();
    bool is_entity_visible_to_player(EntityId player_eid,
                                     Vec2f world_pos) const;

    void register_player(EntityId _) {}
    void remove_player(EntityId pid)
    {
        world_.entity(pid).destruct();
        dirty_entities_.erase(pid);
    }
    bool is_player(EntityId eid) const
    {
        return world_.entity(eid).has<Player>();
    }

    EntityId load_world(SaveManager::SaveData const &data);
    std::vector<SaveManager::NPCData> collect_npc_save_data();

    Server *server() { return server_.get(); }

    // World state access
    WorldState const &world_state() const { return world_state_; }
    QuestManager const &quests() const { return quests_; }
    DialogueEngine &dialogue_engine() { return dialogue_engine_; }
    DialogueState &dialogue(EntityId pid) { return player_dialogues_[pid]; }
    DialogueState const &dialogue(EntityId pid) const
    {
        static DialogueState empty;
        auto it = player_dialogues_.find(pid);
        return it != player_dialogues_.end() ? it->second : empty;
    }
    std::vector<EntityId> npc_entities() const
    {
        std::vector<EntityId> result;
        world_.query<NPCState>().each(
            [&](flecs::entity e, NPCState &) { result.push_back(e.id()); });
        return result;
    }
    void clear_npc_list()
    {
        world_.query<NPCState>().each(
            [](flecs::entity e, NPCState &) { e.destruct(); });
    }

    void set_navigation(NavigationSystem const *nav);

  private:
    flecs::world world_;

    // Owned game systems
    WorldState world_state_;
    KnowledgeGraph knowledge_;
    DialogueEngine dialogue_engine_;
    FactionNetwork factions_;
    CombatSystem combat_;
    QuestManager quests_;
    std::unique_ptr<EventSimulator> events_;
    std::unique_ptr<RumorPropagator> rumors_;
    std::unique_ptr<Server> server_;

    // Persistent flecs system entities (registered once in init_world)
    flecs::entity survival_sys_;
    flecs::entity soldier_ai_sys_;
    flecs::entity combat_resolution_sys_;
    flecs::entity movement_sys_;
    flecs::entity collision_sys_;
    flecs::entity death_marker_sys_;

    std::unique_ptr<CollisionSystem> collision_system_;

    std::unordered_map<EntityId, DialogueState> player_dialogues_;
    TopicRegistry topic_registry_;
    RelationshipTable relationships_;

    std::unordered_set<EntityId> dirty_entities_;
    std::unordered_map<EntityId, std::unordered_set<Vec2i>>
        player_visible_tiles_;
    std::unordered_map<EntityId, std::unordered_set<Vec2i>>
        player_explored_tiles_;
    int soldier_idx_ = 0;
    size_t last_event_count_ = 0;
    std::chrono::duration<float> dt_{}; // In seconds
    NavigationSystem const *navigation_ = nullptr;
    std::vector<CombatEvent> pending_combat_events_;
    std::vector<Projectile> projectiles_;
    size_t last_projectile_count_ = 0;

    Formation &player_formation(EntityId player, uint8_t role);
    char const *formation_name(EntityId player, uint8_t role);
    std::unordered_map<EntityId,
                       std::unordered_map<uint8_t, std::unique_ptr<Formation>>>
        player_formations_;

    void mark_dirty(EntityId eid) { dirty_entities_.insert(eid); }
    void check_event_spawns();
    uint8_t entity_kind(flecs::entity e) const;
    void serialize_entity(flecs::entity e, std::vector<uint8_t> &out) const;
    EntityId find_nearest_interactable(EntityId player, Vec2f player_pos);
};
