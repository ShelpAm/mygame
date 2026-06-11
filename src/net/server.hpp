#pragma once

#include "entities/entity-manager.hpp"
#include "net/network-transport.hpp"
#include "net/transport.hpp"
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CombatSystem;
class WorldState;
class QuestManager;
class EventSimulator;
class GameMode;
class LocaleManager;
class ConditionTracker;
class Client;

class Server {
  public:
    Server();
    ~Server();

    void set_managers(CombatSystem *cs, WorldState *ws, QuestManager *qm);
    void set_event_simulator(EventSimulator *ev);
    void set_game_mode(GameMode *gm);
    void set_survival(ConditionTracker *s)
    {
        survival_ = s;
    }

    awaitable<void> listen(std::uint16_t port);

    void update(float dt);
    void handle_combat_event(int attacker_id, int defender_id, int damage,
                             bool killed);

    void attach_local_pair(Client &client);
    void clear_transports();

    EntityId add_player(Vec2f pos);
    void update_player(EntityId player_id, Vec2f pos, int hp, int max_hp,
                       bool alive);
    void mark_needs_full_sync()
    {
        needs_full_sync_ = true;
    }
    bool check_needs_full_sync()
    {
        bool v = needs_full_sync_;
        needs_full_sync_ = false;
        return v;
    }

  private:
    void attach_transport(std::unique_ptr<ITransport> t);

    std::unordered_set<EntityId> player_entities_;
    std::unordered_map<EntityId, bool> sent_initial_sync_;
    bool needs_full_sync_ = false;
    CombatSystem *cs_ = nullptr;
    WorldState *ws_ = nullptr;
    QuestManager *qm_ = nullptr;
    EventSimulator *events_ = nullptr;
    GameMode *game_mode_ = nullptr;
    ConditionTracker *survival_ = nullptr;
    size_t last_event_count_ = 0;
    int soldier_idx_ = 0;

    struct DeltaPos {
        EntityId pid;
        float mx, my;
    };
    std::queue<DeltaPos> pending_inputs_;

    std::unique_ptr<NetworkTransport::Acceptor> acceptor_;
    std::vector<std::unique_ptr<ITransport>> transports_;

    std::vector<uint8_t> build_sync_payload();
    void broadcast_sync();
    void on_message(TransportExMessage const &msg);
    void check_event_spawns();
};
