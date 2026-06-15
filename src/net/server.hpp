#pragma once

#include "net/network-transport.hpp"
#include "net/transport-guard.hpp"
#include "net/transport.hpp"
#include <memory>
#include <unordered_map>

class GameMode;

class Server {
    friend class GameMode;

  public:
    Server();
    ~Server();

    void set_game_mode(GameMode *gm);

    awaitable<void> listen(std::uint16_t port);
    void stop_listen()
    {
        if (acceptor_) {
            acceptor_->stop(); // No 'guard' class here because I'm lazy. :)
            acceptor_.reset();
        }
    }

    awaitable<bool> authenticate_transport(std::shared_ptr<ITransport> t);

    awaitable<void> attach_transport(std::shared_ptr<ITransport> t);
    void detach_transport(ITransport *t);
    void clear_transports();
    void kick(std::shared_ptr<ITransport> t, std::string const &reason);

    void mark_needs_full_sync(std::string reason)
    {
        needs_full_sync_ = {true, reason};
    }
    bool check_needs_full_sync()
    {
        bool v = needs_full_sync_.first;
        needs_full_sync_.first = false;
        return v;
    }

    auto const &transports_guards() const
    {
        return transport_guards_;
    }

    auto &messages()
    {
        return messages_;
    }

  private:
    std::unordered_map<EntityId, bool> sent_initial_sync_;
    std::pair<bool, std::string> needs_full_sync_{false, ""};
    GameMode *game_mode_ = nullptr;

    deferred_concurrent_channel<void(boost::system::error_code,
                                     std::shared_ptr<ITransport>,
                                     TransportMessage)>
        messages_;

    std::unordered_map<ITransport *, EntityId> player_transport_;
    std::uint8_t next_team_;
    std::shared_ptr<NetworkTransport::Acceptor> acceptor_;
    std::vector<TransportGuard>
        transport_guards_; // Ensures lifetime of transports.

    void broadcast_sync();
    void broadcast_entity_removed(EntityId eid);
    void handle_message(std::shared_ptr<ITransport> from, TransportMessage msg);
    void send_dialogue_to(std::shared_ptr<ITransport> to, EntityId pid);
};
