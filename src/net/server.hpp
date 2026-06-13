#pragma once

#include "entities/entity-manager.hpp"
#include "net/network-transport.hpp"
#include "net/transport.hpp"
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <memory>
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

    void attach_transport(std::unique_ptr<ITransport> t);
    void clear_transports();

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

    std::vector<ITransport *> const &transports() const
    {
        return transports_;
    }

  private:
    std::unordered_set<EntityId> player_entities_;
    std::unordered_map<EntityId, bool> sent_initial_sync_;
    bool needs_full_sync_ = false;
    CombatSystem *cs_ = nullptr;
    WorldState *ws_ = nullptr;
    QuestManager *qm_ = nullptr;
    EventSimulator *events_ = nullptr;
    GameMode *game_mode_ = nullptr;
    ConditionTracker *survival_ = nullptr;
    int soldier_idx_ = 0;

    using deferred_concurrent_channel =
        default_token::as_default_on_t<asio::experimental::concurrent_channel<
            void(boost::system::error_code, ITransport *, TransportMessage)>>;
    deferred_concurrent_channel messages_;

    std::unique_ptr<NetworkTransport::Acceptor> acceptor_;
    std::vector<ITransport *> transports_;

    void broadcast_sync();
    awaitable<void> handle_message(ITransport &from, TransportMessage msg);
};
