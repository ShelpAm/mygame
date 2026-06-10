#pragma once

#include "entities/components/combat-stats.hpp"
#include "entities/entity-manager.hpp"
#include "net/transport.hpp"
#include <memory>
#include <unordered_map>

class CombatSystem;
class WorldState;
class QuestManager;
class NetworkManager;

class Client {
  public:
    Client();

    void update(float dt);
    void handle_combat_event(int attacker_id, int defender_id, int damage,
                             bool killed);
    void attach_local(ITransport *t);
    void attach_network(NetworkManager &net);
    void detach_transport();
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

  private:
    EntityManager em_;
    EntityId player_id_ = invalid_entity;
    std::unordered_map<int, EntityId> id_map_;
    std::unique_ptr<ITransport> transport_;

    void apply_sync(std::vector<uint8_t> const &data);
    void on_message(TransportExMessage const &msg);
};
