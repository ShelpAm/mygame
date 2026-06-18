#pragma once

#include "net/network-session.hpp"
#include "net/session.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>

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

    awaitable<bool> authenticate_transport(std::shared_ptr<Session> t);

    awaitable<void> attach_transport(std::shared_ptr<Session> t);

    void clear_transports();
    void kick(std::shared_ptr<Session> t, std::string const &reason);

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

    auto const &sessions() const { return sessions_; }

    auto &messages() { return messages_; }

    auto &player_detachments() { return player_detachments_; }

  private:
    // Only the read_loop in attach_transport may construct this token
    struct detach_token {
        explicit detach_token() = default;
    };

    // For developer of this class:
    //   Don't call this directly, use kick() or close the connection instead.
    void detach_transport(detach_token, Session *t);

    std::unordered_map<EntityId, bool> sent_initial_sync_;
    std::pair<bool, std::string> needs_full_sync_{false, ""};
    GameMode *game_mode_ = nullptr;

    deferred_concurrent_channel<void(
        boost::system::error_code, std::shared_ptr<Session>, TransportMessage)>
        messages_;
    deferred_concurrent_channel<void(boost::system::error_code, EntityId)>
        player_detachments_;

    std::unordered_map<Session *, EntityId> player_eid_of_session_;
    std::unordered_map<Session *, std::unordered_set<EntityId>>
        last_sent_entities_;
    std::uint8_t next_player_team_;
    std::shared_ptr<NetworkSession::Acceptor> acceptor_;
    std::vector<std::shared_ptr<Session>> sessions_;

    void broadcast_sync();
    void broadcast_entity_removed(EntityId eid);
    void handle_message(std::shared_ptr<Session> from, TransportMessage msg);
    void send_dialogue_to(std::shared_ptr<Session> to, EntityId pid);
};
