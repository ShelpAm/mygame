#include "net/server.hpp"
#include "core/game-mode.hpp"
#include "net/net-packet.hpp"
#include "systems/combat-system.hpp"
#include <cassert>
#include <cstring>
#include <spdlog/spdlog.h>

Server::Server()
    : messages_(ITransport::io(), 128),
      next_team_{static_cast<std::uint8_t>(Team::player_begin)}
{
}
Server::~Server()
{
    clear_transports();

    // Should wait until next iter of ioc, or triggers `read after free`
}

void Server::set_combat_system(CombatSystem *cs)
{
    cs_ = cs;
}

void Server::set_game_mode(GameMode *gm)
{
    game_mode_ = gm;
}

awaitable<void> Server::listen(std::uint16_t port)
{
    acceptor_ = std::make_unique<NetworkTransport::Acceptor>(port);
    spdlog::info("Waiting for incoming connections on port {}...", port);
    try {
        while (acceptor_) {
            auto t = co_await acceptor_->accept();
            spdlog::info("Accepted new connection from {}",
                         t->socket().remote_endpoint().address().to_string());
            attach_transport(std::move(t));
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

void Server::attach_transport(std::unique_ptr<ITransport> uniq_t)
{
    auto t = std::shared_ptr(std::move(uniq_t));
    transport_guards_.push_back(TransportGuard{t});
    spdlog::info("Server: Transport ({}) attached",
                 static_cast<void *>(t.get()));
    // Note to ensure that `t` and `s` should outlive this coro.
    auto read_loop = [](Server *s,
                        std::shared_ptr<ITransport> t) -> awaitable<void> {
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
            if (e.code() == asio::error::operation_aborted ||
                e.code() == asio::error::eof ||
                e.code() == asio::experimental::error::channel_closed ||
                e.code() == asio::experimental::error::channel_cancelled) {
                s->detach_transport(t.get());
                co_return;
            }
            throw;
        }
    };
    ITransport::spawn(read_loop(this, t));
}

void Server::detach_transport(ITransport *t)
{
    player_transport_.erase(t);
    std::erase_if(transport_guards_,
                  [t](auto const &e) { return e.get() == t; });
    spdlog::info("Server: transport {} detached", t->remote_info());
}

void Server::kick(ITransport *t, std::string const &reason)
{
    spdlog::info("Server: kicking transport {}", static_cast<void *>(t));
    // Clean up player entity in ECS and notify remaining clients
    auto node = player_transport_.extract(t);
    if (node.empty())
        throw std::runtime_error(
            "Server::kick: transport not found in player_transport_");

    auto eid = node.mapped();
    game_mode_->remove_player(eid);
    // // Broadcast to all transports EXCEPT the one being kicked
    std::vector<uint8_t> payload;
    write_bytes(payload, eid);
    for (auto &tg : transport_guards_)
        if (tg.get() != t)
            ITransport::spawn(
                tg.get()->write({NetPacket::entity_removed, payload}));
    // Send kicked packet then disconnect (sequential, on io_context)
    std::vector<uint8_t> reason_payload(reason.begin(), reason.end());
    ITransport::spawn([](ITransport *t, auto payload) -> awaitable<void> {
        co_await t->write({NetPacket::kicked, std::move(payload)});
        t->close();
    }(t, std::move(reason_payload)));
}

void Server::clear_transports()
{
    transport_guards_.clear();
}

void Server::handle_message(ITransport &from, TransportMessage msg)
{
    spdlog::debug("Server: handling message '{}'", msg.type);
    if (msg.type == NetPacket::join) {
        assert(msg.payload.empty());
        Team team = static_cast<Team>(next_team_++);
        auto eid = game_mode_->spawn_player({0, 0}, team);
        game_mode_->register_player(eid);
        player_transport_[&from] = eid;
        std::vector<uint8_t> payload;
        write_bytes(payload, eid);
        write_bytes(payload, static_cast<std::uint8_t>(team));
        ITransport::spawn(
            from.write({NetPacket::return_pid, std::move(payload)}));
        spdlog::info("Server: new player joined with ID: {} team: {}", eid,
                     static_cast<std::uint8_t>(team));
    }
    else if (msg.type == NetPacket::entity_update) {
        auto u = parse_entity_update(msg.payload);
        assert(game_mode_->is_player(u.id));
        game_mode_->sync_entity_state(u.id, {u.x, u.y}, u.hp, u.max_hp,
                                      u.alive);
        if (!sent_initial_sync_.contains(u.id) || !sent_initial_sync_[u.id]) {
            sent_initial_sync_[u.id] = true;
            mark_needs_full_sync();
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
        auto it = player_transport_.find(&from);
        if (it == player_transport_.end())
            return;
        game_mode_->handle_interaction(it->second);
        send_dialogue_to(from, it->second);
    }
    else if (msg.type == NetPacket::dialogue_action) {
        auto it = player_transport_.find(&from);
        if (it != player_transport_.end()) {
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
        mark_needs_full_sync();
    }
    else if (msg.type == NetPacket::combat_event) {
        auto ev = parse_combat_event(msg.payload);
        game_mode_->apply_damage(ev.defender_id, ev.damage, ev.killed);
    }
}

void Server::broadcast_sync()
{
    static int frame_counter = 0;
    bool needs_full = check_needs_full_sync();

    NetPacket::Type pkt_type;
    std::vector<uint8_t> payload;
    if (needs_full || ++frame_counter >= 300) {
        payload = game_mode_->build_full_payload();
        pkt_type = NetPacket::state_full;
        frame_counter = 0;
    }
    else if (game_mode_->has_dirty_entities()) {
        payload = game_mode_->build_dirty_payload();
        pkt_type = NetPacket::state_delta;
    }
    else {
        return;
    }

    game_mode_->mark_frame_clean();

    if (payload.empty())
        return;
    for (auto &tg : transport_guards_) {
        ITransport::spawn(tg.get()->write({pkt_type, payload}));
    }
}

void Server::broadcast_entity_removed(EntityId eid)
{
    std::vector<uint8_t> payload;
    write_bytes(payload, eid);
    for (auto &tg : transport_guards_)
        ITransport::spawn(
            tg.get()->write({NetPacket::entity_removed, payload}));
}

void Server::send_dialogue_to(ITransport &to, EntityId pid)
{
    std::vector<uint8_t> payload;
    serialize_dialogue_sync(payload, game_mode_->dialogue(pid));
    ITransport::spawn(to.write({NetPacket::dialogue_sync, std::move(payload)}));
}
