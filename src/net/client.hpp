#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "net/transport.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/combat-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"
#include <memory>
#include <SDL3/SDL.h>
#include <string>
#include <vector>

class App;

struct RemoteEntity {
    EntityId id = 0;
    uint8_t kind =
        0; // EntityKind: 1=player, 2=soldier, 3=npc, 4=enemy, 5=structure

    Vec2f position;   // interpolated (for rendering)
    Vec2f target_pos; // server-authoritative target

    int hp = 20, max_hp = 20;
    bool alive = true;
    Team team = Team::neutral;

    // Movement (optional, set when synced)
    Vec2f velocity{0, 0};
    bool moving = false;

    // SoldierAI (optional, set when synced)
    EntityId follow_target = 0;
    Vec2f formation_offset{0, 0};
    bool in_combat = false;

    // Interactable
    bool interactable = false;

    // Visual (derived from kind + team)
    SDL_FColor color{0.3f, 0.5f, 0.9f, 1.f};
    float scale = 1.f;
    bool hit_flash = false;
};

class Client {
  public:
    Client(App *app);
    ~Client();

    void update(float dt);
    void handle_combat_event(EntityId attacker_id, EntityId defender_id,
                             int damage, bool killed);

    EntityId player_id() const
    {
        return player_id_;
    }

    void send_join_request();
    void send_player_direction(Vec2f dir);
    void send_interact();
    void send_rest();
    void send_recruit();
    void send_chat(std::string const &msg);
    void send_dialogue_action(std::string const &action);

    EntityId local_player() const;
    Vec2f player_position();
    bool is_player_dead();
    CombatStats const *player_stats();

    void attach_transport(std::unique_ptr<ITransport> t);
    void detach_transport();
    ITransport *transport() const
    {
        return transport_.get();
    }

    void interpolate_entities(float dt);
    std::vector<RemoteEntity> &remote_entities()
    {
        return remote_entities_;
    }
    std::vector<RemoteEntity> const &remote_entities() const
    {
        return remote_entities_;
    }
    WorldState &world_state()
    {
        return world_state_;
    }
    WorldState const &world_state() const
    {
        return world_state_;
    }
    void reveal_tile(Vec2i tile)
    {
        world_state_.reveal_tile(tile);
    }

    SurvivalState const &survival() const
    {
        return player_survival_;
    }
    void set_quests(QuestManager const *q)
    {
        quests_ = q;
    }
    QuestManager const &quests() const
    {
        return *quests_;
    }
    std::vector<CombatEvent> &combat_events()
    {
        return combat_events_;
    }
    std::vector<CombatEvent> const &combat_events() const
    {
        return combat_events_;
    }
    std::vector<std::string> const &chat_history() const
    {
        return chat_history_;
    }
    DialogueState const &dialogue() const
    {
        return dialogue_;
    }
    DialogueState &dialogue()
    {
        return dialogue_;
    }

  private:
    App *app_;
    WorldState world_state_;
    SurvivalState player_survival_;
    QuestManager const *quests_ = nullptr;

    EntityId player_id_ = invalid_entity;
    Vec2f player_pos_, player_target_pos_;
    Vec2f player_velocity_{0, 0};
    bool player_moving_ = false;
    int player_hp_ = 20, player_max_hp_ = 20;
    int player_attack_ = 4, player_defense_ = 3;
    float player_attack_range_ = 80.f;
    bool player_alive_ = true;

    std::unique_ptr<ITransport> transport_;
    std::vector<RemoteEntity> remote_entities_;
    std::vector<CombatEvent> combat_events_;
    std::vector<std::string> chat_history_;
    DialogueState dialogue_;

    // Deferred sync processing (io_context thread → main thread)
    deferred_concurrent_channel<void(boost::system::error_code, ITransport *,
                                     TransportMessage)>
        messages_;

    void handle_message(ITransport &from, TransportMessage const &msg);
    void apply_sync_full(std::vector<uint8_t> const &data);
    void apply_sync_delta(std::vector<uint8_t> const &data);
    void handle_entity_update(NetPacket const &pkt);
    void handle_dialogue_sync(std::vector<uint8_t> const &data);

    RemoteEntity *find_entity(EntityId id);
};
