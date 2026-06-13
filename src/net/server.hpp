#pragma once

#include "net/network-transport.hpp"
#include "net/transport.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

class CombatSystem;
class GameMode;

class Server {
    friend class GameMode;

  public:
    Server();
    ~Server();

    void set_combat_system(CombatSystem *cs);
    void set_game_mode(GameMode *gm);

    awaitable<void> listen(std::uint16_t port);

    void attach_transport(std::unique_ptr<ITransport> t);
    void clear_transports();

    void kick(ITransport *t, std::string const &reason);

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

    std::vector<std::unique_ptr<ITransport>> const &transports() const
    {
        return transports_;
    }

    auto &messages()
    {
        return messages_;
    }

  private:
    std::unordered_map<EntityId, bool> sent_initial_sync_;
    bool needs_full_sync_ = false;
    CombatSystem *cs_ = nullptr;
    GameMode *game_mode_ = nullptr;

    deferred_concurrent_channel<void(boost::system::error_code, ITransport *,
                                     TransportMessage)>
        messages_;

    std::unordered_map<ITransport *, EntityId> player_transport_;
    std::unique_ptr<NetworkTransport::Acceptor> acceptor_;
    std::vector<std::unique_ptr<ITransport>> transports_;

    void broadcast_sync();
    void broadcast_entity_removed(EntityId eid);
    void handle_message(ITransport &from, TransportMessage msg);
};
