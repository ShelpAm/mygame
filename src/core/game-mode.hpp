#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "entities/components/collider.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/player.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "systems/combat-system.hpp"
#include "systems/combat-utils.hpp"
#include "systems/quest-manager.hpp"
#include "net/sync-utils.hpp"
#include "world/world-state.hpp"
#include <chrono>
#include <cstdint>
#include <flecs.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EventSimulator;
class RumorPropagator;
struct LocationDefinition;
class MapData;
class Server;
class NavigationSystem;
class CollisionSystem;
struct Formation;

struct FormationKey {
    EntityId captain_id;
    SoldierRole soldier_role;
    EntityId target;

    constexpr auto operator<=>(FormationKey const &) const = default;
};

namespace std {
template <> struct hash<FormationKey> {
    size_t operator()(FormationKey const &key) const noexcept
    {
        // 使用已有的哈希组合，例如 boost::hash_combine 或手动组合
        auto h1 = hash<decltype(key.captain_id)>{}(key.captain_id);
        auto h2 = hash<SoldierRole>{}(key.soldier_role);
        auto h3 = hash<decltype(key.target)>{}(key.target);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};
} // namespace std

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

    void update(float dt);

    EntityId spawn_player(Vec2f pos, Team team);
    EntityId spawn_recruit(EntityId leader);
    EntityId spawn_recruit_ranged(EntityId leader);
    void cycle_stance(EntityId leader);
    void cycle_formation(EntityId player, uint8_t role_mask);

    void handle_interaction(EntityId player);
    void do_dialogue_action(EntityId player, std::string const &action);

    void apply_player_input(EntityId entity, Vec2f dir);
    void apply_damage(EntityId target, int damage, bool killed);
    void heal_entity(EntityId entity, int amount);
    void respawn_player(EntityId pid);

    void sync_entity_state(EntityId entity, Vec2f pos, int hp, int max_hp, bool alive);

    bool has_dirty_entities() const;
    void mark_frame_clean();

    /// Assemble SyncState for sync_util::build_dirty_payload / sync_util::build_full_payload.
    sync_util::SyncState sync_state() const;

    void register_player(EntityId _) {}

    void remove_player(EntityId pid)
    {
        world_.entity(pid).destruct();
        dirty_entities_.erase(pid);
    }
    bool is_player(EntityId eid) const { return world_.entity(eid).has<PlayerTag>(); }

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

    void set_navigation(NavigationSystem const *nav);
    void set_server(Server *s) { server_ = s; }
    void set_map_data(MapData const *md) { map_data_ = md; }

    void set_location_defs(std::vector<LocationDefinition> const &defs) { location_defs_ = &defs; }

  private:
    EntityId spawn_soldier(EntityId captain_id, SoldierRole role);
    EntityId spawn_npc(std::string const &id, std::string const &name, float x, float y,
                       std::string const &personality,
                       std::vector<NPCKnowledgeEntry> const &known_facts);
    void spawn_guards(EntityId captain_eid, int count);
    /// @brief When a player changes formation or new soldier joins in the
    /// formation, this function recalculates the positions of all soldiers in
    /// the formation.
    ///
    /// Formations are determined by the captain, the role and the target at the
    /// same time.
    void relayout_formation(EntityId captain_id);
    void end_dialogue(EntityId player);
    void spawn_enemy_wave(int count, Vec2f center, float spread, Team team);
    std::vector<EntityId> npc_entities() const;

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
    Server *server_ = nullptr;

    // Persistent flecs system entities (registered once in init_world)
    flecs::entity survival_sys_;
    flecs::entity soldier_ai_sys_;
    flecs::entity combat_resolution_sys_;
    flecs::entity movement_sys_;
    flecs::entity collision_sys_;
    flecs::entity death_marker_sys_;
    flecs::entity town_proximity_sys_;
    flecs::entity player_visibility_sys_;

    std::unique_ptr<CollisionSystem> collision_system_;
    flecs::query<Transform, Collider> entity_query_;

    std::unordered_map<EntityId, DialogueState> player_dialogues_;
    TopicRegistry topic_registry_;
    RelationshipTable relationships_;

    std::unordered_set<EntityId> dirty_entities_;
    std::unordered_map<EntityId, std::unordered_set<Vec2i>> player_visible_tiles_;
    std::unordered_map<EntityId, std::unordered_set<Vec2i>> player_explored_tiles_;
    size_t last_event_count_ = 0;
    std::chrono::duration<float> dt_{}; // In seconds
    NavigationSystem const *navigation_ = nullptr;
    float elapsed_ = 0.F;
    std::vector<CombatEvent> pending_combat_events_;
    std::vector<Projectile> projectiles_;
    size_t last_projectile_count_ = 0;

    Formation &formation(EntityId captain_id, SoldierRole role, EntityId target_id);
    std::unordered_map<FormationKey, std::unique_ptr<Formation>> formations_;

    std::unordered_map<EntityId, std::string> current_town_for_player_;
    std::vector<LocationDefinition> const *location_defs_ = nullptr;
    std::vector<std::pair<std::string, std::string>> pending_town_discoveries_;
    bool pending_town_left_ = false;
    MapData const *map_data_ = nullptr;

    void check_event_spawns();
    void spawn_building_entities();
    void spawn_town_npcs();
    EntityId find_nearest_interactable(EntityId player, Vec2f player_pos);
    void mark_dirty(EntityId eid) { dirty_entities_.insert(eid); }
};
