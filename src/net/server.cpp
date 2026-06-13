#include "net/server.hpp"
#include "core/game-mode.hpp"
#include "factions/event-simulator.hpp"
#include "net/client.hpp"
#include "systems/combat-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"
#include <cassert>
#include <cstring>
#include <spdlog/spdlog.h>

Server::Server() : messages_(ITransport::io(), 128) {}
Server::~Server() {}

void Server::set_managers(CombatSystem *cs, WorldState *ws, QuestManager *qm)
{
    cs_ = cs;
    ws_ = ws;
    qm_ = qm;
}

void Server::set_event_simulator(EventSimulator *ev)
{
    events_ = ev;
}

void Server::set_game_mode(GameMode *gm)
{
    game_mode_ = gm;
}

awaitable<void> Server::listen(std::uint16_t port)
{
    acceptor_ = std::make_unique<NetworkTransport::Acceptor>(port);
    spdlog::info("Waiting for incoming connections on port {}...", port);
    while (acceptor_) {
        try {
            auto t = co_await acceptor_->accept();
            spdlog::info("Accepted new connection from {}",
                         t->socket().remote_endpoint().address().to_string());
            attach_transport(std::move(t));
        }
        catch (boost::system::system_error const &e) {
            if (e.code() == asio::error::operation_aborted)
                break;
            spdlog::error("Accept error: {}", e.what());
        }
    }
}

void Server::attach_transport(std::unique_ptr<ITransport> t)
{
    auto read_loop = [this](std::unique_ptr<ITransport> t) -> awaitable<void> {
        auto raw = t.get();
        transports_.push_back(raw);
        spdlog::info("Server: Transport ({}) connected",
                     static_cast<void *>(raw));
        try {
            while (true) {
                auto msg = co_await t->read();
                spdlog::info("Server: Received message of type {} with payload "
                             "size {} from transport "
                             "({})",
                             static_cast<int>(msg.type), msg.payload.size(),
                             static_cast<void *>(raw));
                if (!messages_.try_send(boost::system::error_code{}, raw,
                                        msg)) {
                    co_await messages_.async_send(boost::system::error_code{},
                                                  raw, msg);
                }
            }
        }
        catch (boost::system::system_error const &) {
            std::erase_if(transports_,
                          [raw](auto const &e) { return e == raw; });
            spdlog::info("Server: Transport ({}) disconnected",
                         static_cast<void *>(raw));
        }
        spdlog::info("Exiting spawn");
    };
    ITransport::spawn(read_loop(std::move(t)));
}

void Server::clear_transports()
{
    transports_.clear();
}

awaitable<void> Server::handle_message(ITransport &from, TransportMessage msg)
{
    spdlog::info("Processing message of type {} with payload size {}",
                 static_cast<int>(msg.type), msg.payload.size());
    if (msg.type == NetPacket::join) {
        assert(msg.payload.empty());
        auto eid = game_mode_->spawn_player_for_server({0, 0});
        player_entities_.insert(eid);
        game_mode_->register_player(eid);
        std::vector<uint8_t> payload;
        write_bytes(payload, static_cast<uint32_t>(eid));
        co_await from.write({NetPacket::return_pid, payload});
        spdlog::info("New player joined with ID: {}", eid);
    }
    else if (msg.type == NetPacket::entity_update) {
        auto u = parse_entity_update(msg.payload);
        assert(player_entities_.contains(u.id));
        game_mode_->sync_entity_state(u.id, {u.x, u.y}, u.hp, u.max_hp,
                                       u.alive);
        if (!sent_initial_sync_.contains(u.id) || !sent_initial_sync_[u.id]) {
            sent_initial_sync_[u.id] = true;
            mark_needs_full_sync();
        }
    }
    else if (msg.type == NetPacket::recruit_soldier) {
        assert(msg.payload.size() >= 4);
        uint32_t pid;
        memcpy(&pid, msg.payload.data(), 4);
        assert(pid != invalid_entity);
        game_mode_->spawn_soldier(pid, soldier_idx_++, {0, -1}, {32.f, 32.f});
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
        assert(msg.payload.size() >= 4);
        uint32_t pid;
        memcpy(&pid, msg.payload.data(), 4);
        assert(pid != invalid_entity);
        game_mode_->handle_interaction(pid);
    }
    else if (msg.type == NetPacket::rest) {
        assert(msg.payload.size() >= 4);
        uint32_t pid;
        memcpy(&pid, msg.payload.data(), 4);
        game_mode_->heal_entity(pid, 5);
        mark_needs_full_sync();
    }
    else if (msg.type == NetPacket::combat_event) {
        auto ev = parse_combat_event(msg.payload);
        game_mode_->apply_damage(ev.defender_id, ev.damage, ev.killed);
    }
}

void Server::update(float dt)
{
    // Drain channel — process pending messages on main thread
    auto handle_msg = [this](boost::system::error_code ec, ITransport *t,
                             TransportMessage msg) {
        ITransport::spawn(handle_message(*t, std::move(msg)));
    };
    while (messages_.try_receive(handle_msg)) {
    }

    if (!cs_)
        return;

    // Forward combat events to all transports
    for (auto const &ev : cs_->events()) {
        if (ev.killed)
            qm_->report_kill("enemy");
        std::vector<uint8_t> p;
        auto push = [&](auto v) {
            auto *pb = (uint8_t *)&v;
            p.insert(p.end(), pb, pb + sizeof(v));
        };
        push(static_cast<uint32_t>(ev.attacker_id));
        push(static_cast<uint32_t>(ev.defender_id));
        push(ev.damage);
        p.push_back(ev.killed ? 1 : 0);
        for (auto &t : transports_)
            co_spawn(ITransport::io(), t->write({NetPacket::combat_event, p}),
                     detached);
    }

    broadcast_sync();
}

void Server::broadcast_sync()
{
    static int frame_counter = 0;
    bool needs_full = check_needs_full_sync();

    std::vector<uint8_t> payload;
    if (needs_full || ++frame_counter >= 300) {
        payload = game_mode_->build_full_payload();
        frame_counter = 0;
    }
    else if (game_mode_->has_dirty_entities()) {
        payload = game_mode_->build_dirty_payload();
    }
    else {
        return;
    }

    game_mode_->mark_frame_clean();

    if (payload.empty())
        return;
    for (auto &t : transports_) {
        co_spawn(ITransport::io(), t->write({NetPacket::state_full, payload}),
                 detached);
    }
}
