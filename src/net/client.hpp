#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "net/transport.hpp"
#include <flecs.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CombatSystem;
class WorldState;
class QuestManager;
class NetworkManager;

struct RemoteEntity {
    uint32_t id = 0;
    Vec2f position, target_pos;
    int hp = 20, max_hp = 20;
    bool alive = true;
    int team = 0;
};

class Client {
  public:
    Client();

    void update(float dt);
    void handle_combat_event(int attacker_id, int defender_id, int damage,
                             bool killed);
    void reset();

    void set_player_id(EntityId id)
    {
        player_id_ = id;
    }
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

    flecs::world &entities()
    {
        return world_;
    }
    flecs::world const &entities() const
    {
        return world_;
    }
    EntityId local_player() const;
    Vec2f player_position();
    bool is_player_dead();
    CombatStats const *player_stats();

    void attach_transport(std::unique_ptr<ITransport> t);
    void detach_transport();

    void interpolate_entities(float dt);
    std::vector<RemoteEntity> const &remote_entities() const
    {
        return remote_entities_;
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
    flecs::world world_;
    EntityId player_id_ = invalid_entity;
    std::unordered_map<int, EntityId> id_map_;
    std::unique_ptr<ITransport> transport_;
    std::vector<RemoteEntity> remote_entities_;
    std::vector<std::string> chat_history_;
    DialogueState dialogue_;

    void apply_sync(std::vector<uint8_t> const &data);
    void on_message(ITransport &from, TransportMessage const &msg);
    void handle_entity_update(NetPacket const &pkt);
    void handle_dialogue_sync(std::vector<uint8_t> const &data);
};
