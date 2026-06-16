#include "net/server.hpp"
#include "core/game-mode.hpp"
#include "net/net-packet.hpp"
#include <cassert>
#include <cstring>
#include <spdlog/spdlog.h>

Server::Server()
    : messages_(Session::io(), 128),
      player_detachments_(Session::io(), 128),
      next_team_{static_cast<std::uint8_t>(Team::player_begin)}
{
}
Server::~Server()
{
    clear_transports();

    // Should wait until next iter of ioc, or triggers `read after free`
}

void Server::set_game_mode(GameMode *gm)
{
    game_mode_ = gm;
}

awaitable<void> Server::listen(std::uint16_t port)
{
    acceptor_ = std::make_shared<NetworkSession::Acceptor>(port);
    spdlog::info("Waiting for incoming connections on port {}...", port);
    try {
        while (acceptor_) {
            auto t = co_await acceptor_->accept();
            spdlog::info("Accepted new connection from {}", t->remote_info());
            co_await attach_transport(std::move(t));
        }
    }
    catch (boost::system::system_error const &e) {
        if (e.code() == asio::error::operation_aborted) {
            spdlog::info("Server: stopped listening for incoming connections");
            co_return;
        }
        spdlog::error("Accept error: {}", e.what());
        throw;
    }
}

awaitable<void> Server::attach_transport(std::shared_ptr<Session> t)
{
    spdlog::debug("Server: attaching transport {}", t->remote_info());

    if (!co_await authenticate_transport(t))
        throw std::runtime_error(
            "Server: new connection authentication failed, closing "
            "connection");

    spdlog::debug("Server: auth successful ({})", t->remote_info());

    sessions_.push_back(t);
    spdlog::info("Server: transport {} attached", t->remote_info());

    // Note to ensure that `s` should outlive this coro.
    auto read_loop = [](Server *s,
                        std::shared_ptr<Session> t) -> awaitable<void> {
        try {
            while (true) {
                auto msg = co_await t->read();
                // Subtle: if it runs normally, it indicates that this
                // transport hasn't been detached yet, thus transport is
                // still alive. We don't need to check `self`.
                spdlog::info(
                    "Server: received message \"{}\" with payload size "
                    "{} from transport {}",
                    msg.type, msg.payload.size(), t->remote_info());
                if (!s->messages_.try_send(boost::system::error_code{}, t,
                                           msg)) {
                    co_await s->messages_.async_send(
                        boost::system::error_code{}, t, std::move(msg));
                }
            }
        }
        catch (boost::system::system_error const &e) {
            s->detach_transport(detach_token{}, t.get());
            if (e.code() == asio::error::operation_aborted ||
                e.code() == asio::error::eof ||
                e.code() == asio::error::connection_reset ||
                e.code() == asio::experimental::error::channel_closed ||
                e.code() == asio::experimental::error::channel_cancelled) {
                spdlog::debug("Server: transport {} closed because {}",
                              t->remote_info(), e.what());
                co_return; // Normal exits
            }
            throw;
        }
    };

    Session::spawn(read_loop(this, t));
}

void Server::detach_transport(detach_token, Session *t)
{
    // Defer game-level cleanup to the GameMode thread via channel.
    if (auto node = player_eid_of_session_.extract(t); !node.empty())
        player_detachments_.try_send(boost::system::error_code{}, node.mapped());

    auto it = std::ranges::find_if(sessions_,
                                   [t](auto const &e) { return e.get() == t; });
    if (it == sessions_.end())
        return;
    auto keep_alive = *it;
    sessions_.erase(it);
    keep_alive->close();

    spdlog::info("Server: transport {} detached", t->remote_info());
}

void Server::kick(std::shared_ptr<Session> t, std::string const &reason)
{
    spdlog::info("Server: kicking transport {}", static_cast<void *>(t.get()));
    std::vector<uint8_t> reason_payload(reason.begin(), reason.end());
    Session::spawn(
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            try {
                co_await t->write({NetPacket::kicked, std::move(payload)});
            }
            catch (...) {
            }
            t->close();
        }(t, std::move(reason_payload)));
}

void Server::clear_transports()
{
    sessions_.clear();
}

void Server::handle_message(std::shared_ptr<Session> from, TransportMessage msg)
{
    spdlog::debug("Server: handling message '{}'", msg.type);
    if (msg.type == NetPacket::join) {
        assert(msg.payload.empty());
        Team team = static_cast<Team>(next_team_++);
        auto eid = game_mode_->spawn_player({0, 0}, team);
        game_mode_->register_player(eid);
        player_eid_of_session_[from.get()] = eid;
        auto payload = make_return_pid(eid, static_cast<std::uint8_t>(team));
        Session::spawn(
            [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
                co_await t->write({NetPacket::return_pid, std::move(payload)});
            }(from, std::move(payload)));
        spdlog::info("Server: new player joined with ID: {} team: {}", eid,
                     static_cast<std::uint8_t>(team));
        mark_needs_full_sync("new player joined");
    }
    else if (msg.type == NetPacket::entity_update) {
        auto u = parse_entity_update(msg.payload);
        assert(game_mode_->is_player(u.id));
        game_mode_->sync_entity_state(u.id, {u.x, u.y}, u.hp, u.max_hp,
                                      u.alive);
        if (!sent_initial_sync_.contains(u.id) || !sent_initial_sync_[u.id]) {
            sent_initial_sync_[u.id] = true;
            mark_needs_full_sync("entity_update");
        }
    }
    else if (msg.type == NetPacket::recruit_soldier) {
        assert(msg.payload.size() >= 8);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        assert(pid != invalid_entity);
        game_mode_->spawn_recruit(pid);
    }
    else if (msg.type == NetPacket::spawn_enemy_wave) {
        auto w = parse_enemy_wave(msg.payload);
        game_mode_->spawn_enemy_wave(w.count, {w.cx, w.cy}, 400.f,
                                     static_cast<Team>(w.team));
    }
    else if (msg.type == NetPacket::player_input) {
        auto in = parse_player_input(msg.payload);
        game_mode_->apply_player_input(in.pid, {in.mx, in.my});
    }
    else if (msg.type == NetPacket::interact) {
        auto it = player_eid_of_session_.find(from.get());
        if (it == player_eid_of_session_.end())
            return;
        game_mode_->handle_interaction(it->second);
        send_dialogue_to(from, it->second);
    }
    else if (msg.type == NetPacket::dialogue_action) {
        auto it = player_eid_of_session_.find(from.get());
        if (it != player_eid_of_session_.end()) {
            std::string action(msg.payload.begin(), msg.payload.end());
            game_mode_->do_dialogue_action(it->second, action);
            send_dialogue_to(from, it->second);
        }
    }
    else if (msg.type == NetPacket::rest) {
        assert(msg.payload.size() >= 8);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        game_mode_->heal_entity(pid, 5);
        mark_needs_full_sync("player rest");
    }
    else if (msg.type == NetPacket::combat_event) {
        auto ev = parse_combat_event(msg.payload);
        game_mode_->apply_damage(ev.defender_id, ev.damage, ev.killed);
    }
    else if (msg.type == NetPacket::chat) {
        std::string chat_msg(msg.payload.begin(), msg.payload.end());
        spdlog::info("Chat message from {}: {}", from->remote_info(), chat_msg);
        auto payload = make_chat(chat_msg);
        for (auto &s : sessions_)
            Session::spawn([](std::shared_ptr<Session> t,
                              auto payload) -> awaitable<void> {
                co_await t->write({NetPacket::chat, payload});
            }(s, payload));
    }
}

void Server::broadcast_sync()
{
    // static int frame_counter = 0;
    // if (++frame_counter >= 300)
    //     mark_needs_full_sync("periodic full sync (frame_count >= 300)");

    bool needs_full = check_needs_full_sync();

    NetPacket::Type pkt_type;
    std::vector<uint8_t> payload;
    if (needs_full) {
        // reason
        spdlog::debug("Server: full sync reason: {}", needs_full_sync_.second);
        payload = game_mode_->build_full_payload();
        pkt_type = NetPacket::state_full;
        // frame_counter = 0;
    }
    else if (game_mode_->has_dirty_entities()) {
        spdlog::trace("Server: dirty update");
        payload = game_mode_->build_dirty_payload();
        pkt_type = NetPacket::state_delta;
    }
    else {
        return;
    }

    game_mode_->mark_frame_clean();

    if (payload.empty())
        return;
    for (auto &s : sessions_) {
        Session::spawn([](std::shared_ptr<Session> t, NetPacket::Type pkt_type,
                          std::vector<uint8_t> payload) -> awaitable<void> {
            co_await t->write({pkt_type, payload});
        }(s, pkt_type, payload));
    }
}

void Server::broadcast_entity_removed(EntityId eid)
{
    auto payload = make_entity_removed(eid);
    for (auto &s : sessions_)
        Session::spawn(
            [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
                co_await t->write({NetPacket::entity_removed, payload});
            }(s, payload));
}

void Server::send_dialogue_to(std::shared_ptr<Session> to, EntityId pid)
{
    std::vector<uint8_t> payload;
    serialize_dialogue_sync(payload, game_mode_->dialogue(pid));
    Session::spawn(
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::dialogue_sync, std::move(payload)});
        }(to, std::move(payload)));
}

awaitable<bool> Server::authenticate_transport(std::shared_ptr<Session> t)
{
    try {
        auto req = co_await t->read();
        spdlog::debug("Server: received auth: type: {}, payload: {}", req.type,
                      req.payload);
        if (req.type != NetPacket::auth || req.payload != auth_payload())
            co_return false;

        co_await t->write({NetPacket::auth, auth_payload()});
        co_return true;
    }
    catch (...) {
        co_return false;
    }
}
