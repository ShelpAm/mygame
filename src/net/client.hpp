#pragma once

#include "animation/animation-data.hpp"
#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/soldier-ai.hpp"
#include "net/session.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/combat-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"
#include <memory>
#include <SDL3/SDL.h>
#include <string>
#include <vector>

class App;

struct ProjectileVisual {
    Vec2f pos;
    Vec2f dst;
    Vec2f dir;
    float speed = 400.f;
    float total_dist = 0.f;
    float traveled = 0.f;
};

struct SnapshotEntity {
    Vec2f position;
    uint8_t kind = 0;
    Vec2f facing{0, -1};
    SDL_FColor color{0.3f, 0.5f, 0.9f, 1.f};
    float scale = 1.f;
    std::string texture_name = "entity";
    Team team = Team::neutral;
    bool alive = true;
};

struct RemoteEntity {
    EntityId id = 0;
    uint8_t kind = 0; // EntityKind: 1=player, 2=soldier, 3=npc, 4=enemy, 5=structure

    Vec2f position;   // interpolated (for rendering)
    Vec2f target_pos; // server-authoritative target

    int hp = 20, max_hp = 20;
    bool alive = true;
    Team team = Team::neutral;

    // Movement (optional, set when synced)
    Vec2f velocity{0, 0};
    Vec2f facing{0, -1};

    // Interactable
    bool interactable = false;

    // Soldier state (synced from SoldierAI component)
    SoldierStance soldier_stance = SoldierStance::defensive;
    SoldierRole soldier_role = SoldierRole::melee;
    int vision_range = 6;
    float vision_arc = 180.f;

    // Visual (derived from kind + team, overridden by synced Sprite)
    SDL_FColor color{0.3f, 0.5f, 0.9f, 1.f};
    float scale = 1.f;
    std::string texture_name = "entity";
    bool visible = true;
    bool hit_flash = false;
    AnimationState anim_state;
};

class Client {
  public:
    Client(App *app);
    ~Client();

    void update(float dt);
    void handle_combat_event(EntityId attacker_id, EntityId defender_id, int damage, bool killed);

    EntityId player_id() const { return player_id_; }
    Team player_team() const { return player_team_; }

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

    Vec2f player_position() const;
    bool is_player_dead();
    CombatStats const *player_stats();

    awaitable<void> attach_transport(std::shared_ptr<Session> t);

    /// @brief Asynchronously disconnects from the current session, if exists.
    void close_current_session()
    {
        if (session_) {
            session_->close();
            session_.reset();
        }
    }

    std::string session_remote_info() const
    {
        return session_ ? session_->remote_info() : std::string{};
    }

    void interpolate_entities(float dt);
    std::vector<RemoteEntity> &remote_entities() { return remote_entities_; }
    std::vector<RemoteEntity> const &remote_entities() const { return remote_entities_; }
    // std::vector<SnapshotEntity> &snapshots() { return snapshots_; }
    // std::vector<SnapshotEntity> const &snapshots() const { return snapshots_;
    // }
    WorldState &world_state() { return world_state_; }
    WorldState const &world_state() const { return world_state_; }

    PlayerVisibility &player_visibility() { return player_visibility_; }
    PlayerVisibility const &player_visibility() const { return player_visibility_; }

    SurvivalState const &survival() const { return player_survival_; }
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

  private:
    // Only the read_loop in attach_transport may construct this token
    struct detach_token {
        explicit detach_token() = default;
    };

    // For developer of this class:
    //   Don't call this directly, use kick() or close the connection instead.
    void detach_transport(detach_token, Session *);

    App *app_;
    WorldState world_state_;
    PlayerVisibility player_visibility_;
    SurvivalState player_survival_;
    QuestManager const *quests_ = nullptr;

    EntityId player_id_ = invalid_entity;
    Team player_team_ = Team::invalid_team;
    Vec2f player_pos_, player_target_pos_;
    Vec2f player_velocity_{0, 0};
    Vec2f player_facing_{0, -1};
    int player_hp_ = 20, player_max_hp_ = 20;
    int player_attack_ = 4, player_defense_ = 3;
    float player_attack_range_ = 80.f;
    int player_vision_range_ = 6;
    float player_vision_arc_ = 180.f;
    bool player_alive_ = true;

    // std::queue<TransportGuard> transport_guards_; // Because there could be
    // some connections keeping unclosed, we set a queue here to wait them.

    std::shared_ptr<Session> session_;
    std::vector<RemoteEntity> remote_entities_;
    // std::vector<SnapshotEntity> snapshots_;
    std::vector<CombatEvent> combat_events_;
    std::vector<ProjectileVisual> projectile_visuals_;
    std::vector<std::string> chat_history_;
    uint8_t selected_roles_ = 0xFF; // all selected by default
    int formation_idx_ = 0;
    DialogueState dialogue_;

    // Deferred sync processing (io_context thread → main thread)
    deferred_concurrent_channel<void(boost::system::error_code, std::shared_ptr<Session>,
                                     TransportMessage)>
        messages_;

    void handle_message(Session &from, TransportMessage msg);
    void apply_sync_full(std::vector<uint8_t> const &data);
    void apply_sync_delta(std::vector<uint8_t> const &data);
    void handle_entity_update(NetPacket const &pkt);
    void handle_dialogue_sync(std::vector<uint8_t> const &data);

    RemoteEntity *find_entity(EntityId id);
};
