#include "net/server.hpp"
#include "core/game-mode.hpp"
#include "net/net-packet.hpp"
#include "net/sync-utils.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>

Server::Server()
    : messages_(Session::io(), 128), player_detachments_(Session::io(), 128),
      next_player_team_{static_cast<std::uint8_t>(Team::player_begin)}
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
        throw std::runtime_error("Server: new connection authentication failed, closing "
                                 "connection");

    spdlog::debug("Server: auth successful ({})", t->remote_info());

    sessions_.push_back(t);
    spdlog::info("Server: transport {} attached", t->remote_info());

    // Note to ensure that `s` should outlive this coro.
    auto read_loop = [](Server *s, std::shared_ptr<Session> t) -> awaitable<void> {
        try {
            while (true) {
                auto msg = co_await t->read();
                // Subtle: if it runs normally, it indicates that this
                // transport hasn't been detached yet, thus transport is
                // still alive. We don't need to check `self`.
                spdlog::trace("Server: received message \"{}\" with payload size "
                              "{} from transport {}",
                              static_cast<ClientMsgType>(msg.type), msg.payload.size(), t->remote_info());
                if (!s->messages_.try_send(boost::system::error_code{}, t, msg)) {
                    Session::spawn([](Server *s, auto t, auto msg) -> awaitable<void> {
                        co_await s->messages_.async_send(boost::system::error_code{}, t,
                                                         std::move(msg));
                    }(s, t, std::move(msg)));
                }
            }
        }
        catch (boost::system::system_error const &e) {
            s->detach_transport(detach_token{}, t.get());
            if (e.code() == asio::error::operation_aborted || e.code() == asio::error::eof ||
                e.code() == asio::error::connection_reset ||
                e.code() == asio::experimental::error::channel_closed ||
                e.code() == asio::experimental::error::channel_cancelled) {
                spdlog::debug("Server: transport {} closed because {}", t->remote_info(), e.what());
                co_return; // Normal exits
            }
            throw;
        }
    };

    Session::spawn(read_loop(this, t));
}

void Server::detach_transport(detach_token _, Session *t)
{
    // Defer game-level cleanup to the GameMode thread via channel.
    if (auto node = player_eid_of_session_.extract(t); !node.empty())
        if (!player_detachments_.try_send(boost::system::error_code{}, node.mapped()))
            Session::spawn([](Server *s, auto eid) -> awaitable<void> {
                co_await s->player_detachments_.async_send(boost::system::error_code{}, eid);
            }(this, node.mapped()));

    auto it = std::ranges::find_if(sessions_, [t](auto const &e) { return e.get() == t; });
    if (it == sessions_.end())
        return;
    auto const &keep_alive = *it;
    sessions_.erase(it);
    keep_alive->close();

    spdlog::info("Server: transport {} detached", t->remote_info());
}

void Server::kick(std::shared_ptr<Session> t, std::string const &reason)
{
    (void)this;
    spdlog::info("Server: kicking transport {}", static_cast<void *>(t.get()));
    std::vector<uint8_t> reason_payload(reason.begin(), reason.end());
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        try {
            co_await t->write({ServerMsgType::kicked, std::move(payload)});
        }
        catch (boost::system::system_error const &e) {
            ; // TODO(shelpam): Temporarily leaves empty, as I don't know
              // how to resolve now.
        }
        t->close();
    }(std::move(t), std::move(reason_payload)));
}

void Server::clear_transports()
{
    sessions_.clear();
}

void Server::handle_message(std::shared_ptr<Session> from, TransportMessage msg)
{
    switch (static_cast<ClientMsgType>(msg.type)) {
    case ClientMsgType::join: {
        assert(msg.payload.empty());
        Team team = static_cast<Team>(next_player_team_++);
        auto eid = game_mode_->spawn_player({0, 0}, team);
        game_mode_->register_player(eid);
        player_eid_of_session_[from.get()] = eid;
        auto payload = make_return_pid(eid, static_cast<std::uint8_t>(team));
        Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ServerMsgType::return_pid, std::move(payload)});
        }(from, std::move(payload)));
        spdlog::info("Server: new player joined (ID: {}, team: {})", eid,
                     static_cast<std::uint8_t>(team));
        mark_needs_full_sync("new player joined");
        break;
    }
    case ClientMsgType::entity_update: {
        auto u = parse_entity_update(msg.payload);
        assert(game_mode_->is_player(u.id));
        game_mode_->sync_entity_state(u.id, {u.x, u.y}, u.hp, u.max_hp, u.alive);
        if (!sent_initial_sync_.contains(u.id) || !sent_initial_sync_[u.id]) {
            sent_initial_sync_[u.id] = true;
            mark_needs_full_sync("entity_update");
        }
        break;
    }
    case ClientMsgType::recruit_soldier: {
        assert(msg.payload.size() >= 8);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        assert(pid != invalid_entity);
        spdlog::debug("Server: recruit soldier for player {}", pid);
        game_mode_->spawn_recruit(pid);
        break;
    }
    case ClientMsgType::recruit_ranged: {
        assert(msg.payload.size() >= 8);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        spdlog::debug("Server: recruit ranged for player {}", pid);
        game_mode_->spawn_recruit_ranged(pid);
        break;
    }
    case ClientMsgType::soldier_command: {
        assert(msg.payload.size() >= 8);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        game_mode_->cycle_stance(pid);
        break;
    }
    case ClientMsgType::respawn: {
        assert(msg.payload.size() >= 8);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        spdlog::debug("Server: respawn player {}", pid);
        game_mode_->respawn_player(pid);
        break;
    }
    case ClientMsgType::formation: {
        assert(msg.payload.size() >= 9);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        uint8_t mask = msg.payload[8];
        game_mode_->cycle_formation(pid, mask);
        break;
    }
    case ClientMsgType::player_input: {
        auto in = parse_player_input(msg.payload);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        spdlog::debug("Server: player_input pid={} dir=({:.2f},{:.2f}) "
                      "client_tick={} server_tick={} delay={}ms",
                      in.pid, in.mx, in.my, in.client_ms, now_ms,
                      static_cast<int32_t>(now_ms - in.client_ms));
        game_mode_->apply_player_input(in.pid, {in.mx, in.my});
        break;
    }
    case ClientMsgType::interact: {
        auto it = player_eid_of_session_.find(from.get());
        if (it == player_eid_of_session_.end())
            break;
        game_mode_->handle_interaction(it->second);
        send_dialogue_to(from, it->second);
        break;
    }
    case ClientMsgType::dialogue_action: {
        auto it = player_eid_of_session_.find(from.get());
        if (it != player_eid_of_session_.end()) {
            std::string action(msg.payload.begin(), msg.payload.end());
            game_mode_->do_dialogue_action(it->second, action);
            send_dialogue_to(from, it->second);
        }
        break;
    }
    case ClientMsgType::rest: {
        assert(msg.payload.size() >= 8);
        EntityId pid;
        memcpy(&pid, msg.payload.data(), 8);
        spdlog::debug("Server: player {} rests", pid);
        game_mode_->heal_entity(pid, 5);
        mark_needs_full_sync("player rest");
        break;
    }
    case ClientMsgType::combat_event: {
        auto ev = parse_combat_event(msg.payload);
        spdlog::debug("Server: combat event: attacker={}, defender={}, dmg={}", ev.attacker_id,
                      ev.defender_id, ev.damage);
        game_mode_->apply_damage(ev.defender_id, ev.damage, ev.killed);
        break;
    }
    case ClientMsgType::chat: {
        std::string chat_msg(msg.payload.begin(), msg.payload.end());
        spdlog::info("Chat message from {}: {}", from->remote_info(), chat_msg);
        auto payload = make_chat(chat_msg);
        for (auto &s : sessions_)
            Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
                co_await t->write({ServerMsgType::chat, payload});
            }(s, payload));
        break;
    }
    case ClientMsgType::auth:
        // Auth is handled in authenticate_transport, not here
        spdlog::debug("Server: unexpected auth message in handle_message");
        break;
    }
}

void Server::broadcast_sync()
{
    bool needs_full = check_needs_full_sync();

    // Force a keepalive sync at least every 500ms so clients can track
    // connection health even when nothing is changing.
    auto now = std::chrono::steady_clock::now();
    bool force = now - last_sync_time_ >= std::chrono::milliseconds(500);

    if (!needs_full && !game_mode_->has_dirty_entities() && !force)
        return;
    last_sync_time_ = now;

    if (needs_full)
        spdlog::debug("Server: full sync reason: {}", needs_full_sync_.second);

    for (auto &s : sessions_) {
        auto player_it = player_eid_of_session_.find(s.get());
        if (player_it == player_eid_of_session_.end())
            continue;
        EntityId player_eid = player_it->second;

        ServerMsgType pkt_type{};
        sync_util::SyncPayload result;

        if (needs_full) {
            result = sync_util::build_full_payload(game_mode_->sync_state(), player_eid);
            pkt_type = ServerMsgType::state_full;
        }
        else {
            result = sync_util::build_dirty_payload(game_mode_->sync_state(), player_eid,
                                                last_sent_entities_[s.get()]);
            pkt_type = ServerMsgType::state_delta;
        }

        if (result.bytes.empty())
            continue;

        last_sent_entities_[s.get()] = result.entity_ids;

        Session::spawn([](std::shared_ptr<Session> t, ServerMsgType pkt_type,
                          std::vector<uint8_t> payload) -> awaitable<void> {
            co_await t->write({pkt_type, payload});
        }(s, pkt_type, std::move(result.bytes)));
    }

    game_mode_->mark_frame_clean();
}

void Server::broadcast_entity_removed(EntityId eid)
{
    auto payload = make_entity_removed(eid);
    for (auto &s : sessions_)
        Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ServerMsgType::entity_removed, payload});
        }(s, payload));
}

void Server::broadcast_to_all(ServerMsgType type, std::vector<uint8_t> payload)
{
    for (auto &s : sessions_)
        Session::spawn([](std::shared_ptr<Session> t, ServerMsgType type,
                          std::vector<uint8_t> payload) -> awaitable<void> {
            co_await t->write({type, payload});
        }(s, type, payload));
}

void Server::poll_messages(GameMode &gm)
{
    while (messages_.try_receive(
        [this](boost::system::error_code, std::shared_ptr<Session> t, TransportMessage msg) {
            handle_message(std::move(t), std::move(msg));
        })) {
    }

    while (player_detachments_.try_receive([this, &gm](boost::system::error_code, EntityId eid) {
        gm.remove_player(eid);
        broadcast_entity_removed(eid);
    })) {
    }
}

void Server::send_dialogue_to(std::shared_ptr<Session> to, EntityId pid)
{
    std::vector<uint8_t> payload;
    serialize_dialogue_sync(payload, game_mode_->dialogue(pid));
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        co_await t->write({ServerMsgType::dialogue_sync, std::move(payload)});
    }(to, std::move(payload)));
}

awaitable<bool> Server::authenticate_transport(std::shared_ptr<Session> t)
{
    try {
        auto req = co_await t->read();
        spdlog::debug("Server: received auth: type: {}, payload: {}", static_cast<ClientMsgType>(req.type), req.payload);
        if (req.type != static_cast<std::uint32_t>(ClientMsgType::auth) || req.payload != auth_payload())
            co_return false;

        co_await t->write({ServerMsgType::auth, auth_payload()});
        co_return true;
    }
    catch (...) {
        co_return false;
    }
}
