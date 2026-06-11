#pragma once

#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/entity-manager.hpp"
#include "net/transport.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CombatSystem;
class WorldState;
class QuestManager;
class NetworkManager;

struct RemoteEntity {
    int id = 0;
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
    uint32_t player_id() const
    {
        return player_id_;
    }

    void send_join_request();
    void send_player_direction(Vec2f dir);
    void send_interact();
    void send_rest();
    void send_recruit();
    void send_chat(std::string const &msg);

    EntityManager &entities()
    {
        return em_;
    }
    EntityManager const &entities() const
    {
        return em_;
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

  private:
    EntityManager em_;
    EntityId player_id_ = invalid_entity;
    std::unordered_map<int, EntityId> id_map_;
    std::unique_ptr<ITransport> transport_;
    std::vector<RemoteEntity> remote_entities_;
    std::vector<std::string> chat_history_;

    void apply_sync(std::vector<uint8_t> const &data);
    void on_message(TransportExMessage const &msg);
    void handle_entity_update(NetPacket const &pkt);
};
