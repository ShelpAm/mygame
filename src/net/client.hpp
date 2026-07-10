#pragma once

#include "animation/animation-data.hpp"
#include "core/game-types.hpp"
#include "core/math.hpp"
#include "components/combat-stats.hpp"
#include "components/entity-kind.hpp"
#include "components/interpolation-target.hpp"
#include "components/movement.hpp"
#include "components/soldier-ai.hpp"
#include "components/vision.hpp"
#include "components/survival-state.hpp"
#include "components/visual/animation.hpp"
#include "components/visual/sprite.hpp"
#include "net/session.hpp"
#include "systems/animation-controller-system.hpp"
#include "systems/animation-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/location-store.hpp"
#include "world/world-state.hpp"
#include <flecs.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ResourceManager;

struct ProjectileVisual {
    Vec2f pos;
    Vec2f dst;
    Vec2f dir;
    float speed = 400.f;
    float total_dist = 0.f;
    float traveled = 0.f;
};

class Client {
  public:
    Client(Client const &) = delete;
    Client(Client &&) = delete;
    Client &operator=(Client const &) = delete;
    Client &operator=(Client &&) = delete;
    Client();
    ~Client();

    void update(float dt);
    void handle_combat_event(EntityId attacker_id, EntityId defender_id, int damage, bool killed);

    bool connected() const
    {
        return session_ != nullptr && session_->is_open() && player_id_ != invalid_entity;
    }

    EntityId player_id() const { return player_id_; }

    /// Read team from the ECS player entity.
    Team player_team() const;

    // Verifies authority of server
    static awaitable<bool> authenticate_transport(std::shared_ptr<Session> t);

    void send_join_request();
    void send_player_direction(Vec2f dir);
    void send_interact();
    void send_rest();
    void send_recruit();
    void send_recruit_ranged();
    void send_soldier_command();
    void send_respawn();
    void send_cycle_formation();
    void send_chat(std::string const &msg);
    void send_dialogue_action(std::string const &action);

    // Town discovery
    struct DiscoveredTown {
        std::string loc_id;
        std::string locale_key;
        bool notified = false;
    };
    std::vector<DiscoveredTown> const &discovered_towns() const { return discovered_towns_; }
    std::string const &current_town_id() const { return current_town_id_; }
    void set_location_defs(std::vector<LocationDefinition> const *defs) { location_defs_ = defs; }
    std::vector<LocationDefinition> const *location_defs() const { return location_defs_; }

    /// Read position from the ECS player entity.
    Vec2f player_position() const;

    std::uint64_t rtt_ms() const { return estimated_rtt_ms_; }
    std::uint64_t last_sync_age() const
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now() - last_sync_recv_tick_).count();
    }
    bool is_player_dead();
    CombatStats const *player_stats();

    awaitable<void> attach_transport(std::shared_ptr<Session> t);

    void close_current_session()
    {
        if (session_) {
            session_->close();
            session_.reset();
        }
    }

    std::string session_remote_info() const
    {
        return session_ ? session_->remote_info() : std::string{"`session is null`"};
    }

    void record_sync_received();

    flecs::world &world() { return world_; }
    flecs::world const &world() const { return world_; }

    WorldState &world_state() { return world_state_; }
    WorldState const &world_state() const { return world_state_; }

    PlayerVisibility &player_visibility() { return player_visibility_; }
    PlayerVisibility const &player_visibility() const { return player_visibility_; }

    /// Read survival from the ECS player entity.
    SurvivalState const &survival() const;

    void set_quests(QuestManager const *q) { quests_ = q; }
    QuestManager const &quests() const { return *quests_; }
    std::vector<CombatEvent> &combat_events() { return combat_events_; }
    std::vector<CombatEvent> const &combat_events() const { return combat_events_; }
    std::vector<std::string> const &chat_history() const { return chat_history_; }
    std::vector<ProjectileVisual> &projectile_visuals() { return projectile_visuals_; }
    std::vector<ProjectileVisual> const &projectile_visuals() const { return projectile_visuals_; }
    uint8_t selected_roles() const { return selected_roles_; }
    void toggle_selected_role(uint8_t role) { selected_roles_ ^= (1 << role); }
    int formation_idx() const { return formation_idx_; }
    void set_formation_idx(int idx) { formation_idx_ = idx; }
    DialogueState const &dialogue() const { return dialogue_; }
    DialogueState &dialogue() { return dialogue_; }

    std::function<void()> on_kicked_;
    ResourceManager const *resources_ = nullptr;

    void set_resources(ResourceManager const *res) { resources_ = res; }

  private:
    struct detach_token {
        explicit detach_token() = default;
    };
    void detach_transport(detach_token, Session *);

    // Client-side ECS world — synchronised from server state
    flecs::world world_;
    AnimationSystem anim_sys_;
    AnimationControllerSystem anim_ctrl_sys_;
    flecs::entity interp_sys_;
    flecs::entity anim_ctrl_pipeline_;
    flecs::entity anim_sys_pipeline_;

    WorldState world_state_;
    PlayerVisibility player_visibility_;
    QuestManager const *quests_ = nullptr;

    EntityId player_id_ = invalid_entity;
    /// The server-assigned entity ID for the player (sent in return_pid).
    /// Used when constructing outbound messages so the server can identify
    /// which entity to act on.  player_id_ holds the local ECS entity ID.
    EntityId server_player_id_ = invalid_entity;

    /// Server entity ID → local ECS entity ID mapping.
    /// Maintained on every entity create/destroy so lookups are O(1).
    std::unordered_map<EntityId, flecs::entity_t> server_to_local_;

    // Network diagnostics
    std::chrono::steady_clock::time_point last_sync_recv_tick_{};
    std::uint64_t estimated_rtt_ms_ = 0;
    std::chrono::steady_clock::time_point last_active_send_tick_{};

    std::shared_ptr<Session> session_;
    std::vector<CombatEvent> combat_events_;
    std::vector<ProjectileVisual> projectile_visuals_;
    std::vector<std::string> chat_history_;
    uint8_t selected_roles_ = 0xFF;
    int formation_idx_ = 0;
    DialogueState dialogue_;
    std::vector<LocationDefinition> const *location_defs_ = nullptr;
    std::vector<DiscoveredTown> discovered_towns_;
    std::string current_town_id_;

    deferred_concurrent_channel<void(boost::system::error_code, std::shared_ptr<Session>,
                                     TransportMessage)>
        messages_;

    void handle_message(Session &from, TransportMessage msg);
    void apply_sync_full(std::vector<uint8_t> const &data);
    void apply_sync_delta(std::vector<uint8_t> const &data);
    void handle_entity_update(std::vector<uint8_t> const &payload);
    void handle_dialogue_sync(std::vector<uint8_t> const &data);
};
