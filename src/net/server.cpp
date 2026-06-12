#include "net/server.hpp"
#include "core/game-mode.hpp"
#include "entities/components/interactable.hpp"
#include "entities/components/position.hpp"
#include "factions/event-simulator.hpp"
#include "net/client.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/combat-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"
#include <cassert>
#include <cstring>
#include <spdlog/spdlog.h>

Server::Server() {}
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
    co_spawn(
        ITransport::io(),
        [this, t = t.get()]() -> awaitable<void> {
            spdlog::info("Transport ({}) connected", static_cast<void *>(t));
            while (true) {
                try {
                    co_await on_message(*t, co_await t->read());
                }
                catch (boost::system::system_error const &) {
                    break;
                }
            }
            spdlog::info("Transport ({}) disconnected", static_cast<void *>(t));
            std::erase_if(transports_, [trans = t](auto const &t) {
                return t.get() == trans;
            });
            co_return;
        },
        detached);
    transports_.push_back(std::move(t));
}

void Server::clear_transports()
{
    transports_.clear();
}

awaitable<void> Server::on_message(ITransport &from,
                                   TransportMessage const &msg)
{
    spdlog::info("Processing message of type {} with payload size {}",
                 static_cast<int>(msg.type), msg.payload.size());
    if (msg.type == NetPacket::join) {
        assert(msg.payload.empty());
        auto eid = add_player({0, 0});
        player_entities_.insert(eid);
        std::vector<uint8_t> payload;
        write_bytes(payload, eid); // player_id
        co_await from.write({NetPacket::return_pid, payload});
        spdlog::info("New player joined with ID: {}", eid);
    }
    else if (msg.type == NetPacket::entity_update) {
        auto u = parse_entity_update(msg.payload);
        assert(player_entities_.contains(u.id));
        update_player(u.id, {u.x, u.y}, u.hp, u.max_hp, u.alive);
    }
    else if (msg.type == NetPacket::recruit_soldier) {
        assert(msg.payload.size() >= 4);
        uint32_t pid;
        memcpy(&pid, msg.payload.data(), 4);
        assert(pid != invalid_entity);
        auto eid = game_mode_->spawn_soldier(pid, soldier_idx_++, {0, -1});
        auto *lp = game_mode_->entities().get_component<Position>(pid);
        auto *sp = game_mode_->entities().get_component<Position>(eid);
        assert(lp && sp);
        sp->world_pos = {lp->world_pos.x + 32.f, lp->world_pos.y + 32.f};
    }
    else if (msg.type == NetPacket::spawn_enemy_wave) {
        auto w = parse_enemy_wave(msg.payload);
        assert(cs_);
        cs_->spawn_enemy_wave(game_mode_->entities(), w.count, {w.cx, w.cy},
                              400.f, static_cast<Team>(w.team));
    }
    else if (msg.type == NetPacket::player_input) {
        auto in = parse_player_input(msg.payload);
        pending_inputs_.push({in.pid, in.mx, in.my});
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
        auto *cs = game_mode_->entities().get_component<CombatStats>(pid);
        if (cs && cs->alive) {
            cs->hp = std::min(cs->max_hp, cs->hp + 5);
            if (survival_)
                survival_->heal(5.f);
            mark_needs_full_sync();
        }
    }
    else if (msg.type == NetPacket::combat_event) {
        auto ev = parse_combat_event(msg.payload);
        handle_combat_event(ev.attacker_id, ev.defender_id, ev.damage,
                            ev.killed);
    }
}

void Server::update(float dt)
{
    if (!cs_)
        return;

    // Processes user input
    while (!pending_inputs_.empty()) {
        auto const &[pid, mx, my] = pending_inputs_.front();
        assert(player_entities_.contains(pid));
        assert(game_mode_->entities().alive(pid));
        auto *pp = game_mode_->entities().get_component<Position>(pid);
        assert(pp);
        assert(game_mode_);

        Vec2f new_pos{pp->world_pos.x + mx * 200.f * dt,
                      pp->world_pos.y + my * 200.f * dt};
        game_mode_->apply_player_movement(pid, new_pos);
        pending_inputs_.pop();
    }

    cs_->update(game_mode_->entities(), dt);
    for (auto const &ev : cs_->events()) {
        if (ev.killed)
            qm_->report_kill("enemy");
        std::vector<uint8_t> p;
        auto push = [&](auto v) {
            auto *pb = (uint8_t *)&v;
            p.insert(p.end(), pb, pb + sizeof(v));
        };
        push(ev.attacker_id);
        push(ev.defender_id);
        push(ev.damage);
        p.push_back(ev.killed ? 1 : 0);
        for (auto &t : transports_)
            co_spawn(ITransport::io(), t->write({NetPacket::combat_event, p}),
                     detached);
    }

    check_event_spawns();

    if (needs_full_sync_)
        needs_full_sync_ = false;

    broadcast_sync();
}

void Server::check_event_spawns()
{
    if (!events_)
        return;
    auto cnt = events_->triggered_events().size();
    if (cnt > last_event_count_) {
        last_event_count_ = cnt;
        for (auto pid : player_entities_) {
            auto *pos = game_mode_->entities().get_component<Position>(pid);
            Vec2f center = pos ? pos->world_pos : Vec2f{};
            auto &latest = events_->triggered_events().back();
            if (latest.type == GameEvent::Type::battle ||
                latest.type == GameEvent::Type::refugee_wave)
                cs_->spawn_enemy_wave(game_mode_->entities(), 2 + rand() % 4,
                                      center, 400.f, Team::enemy);
        }
    }
}

void Server::broadcast_sync()
{
    auto payload = build_sync_payload();
    if (payload.empty())
        return;
    for (auto &t : transports_) {
        co_spawn(ITransport::io(), t->write({NetPacket::state_full, payload}),
                 detached);
    }
}

void Server::handle_combat_event(int attacker_id, int defender_id, int damage,
                                 bool killed)
{
    (void)attacker_id;
    auto *cs = game_mode_->entities().get_component<CombatStats>(defender_id);
    if (cs) {
        cs->hp -= damage;
        if (killed)
            cs->alive = false;
    }
}

EntityId Server::add_player(Vec2f pos)
{
    auto eid = game_mode_->entities().create_entity();
    spdlog::info("Adding player {} at position ({}, {})", eid, pos.x, pos.y);
    game_mode_->entities().add_component<Position>(eid,
                                                   Position{pos, {0, 0}, 1.f});
    game_mode_->entities().add_component<CombatStats>(
        eid, CombatStats{Team::player, 20, 20, 4, 3, 80.f});
    mark_needs_full_sync();
    return eid;
}

void Server::update_player(EntityId player_id, Vec2f pos, int hp, int max_hp,
                           bool alive)
{
    auto *p = game_mode_->entities().get_component<Position>(player_id);
    auto *c = game_mode_->entities().get_component<CombatStats>(player_id);
    if (p)
        p->world_pos = pos;
    if (c) {
        c->hp = hp;
        c->max_hp = max_hp;
        c->alive = alive;
    }
    if (!sent_initial_sync_[player_id]) {
        sent_initial_sync_[player_id] = true;
        mark_needs_full_sync();
    }
}

std::vector<uint8_t> Server::build_sync_payload()
{
    std::vector<uint8_t> out;
    auto push = [&](auto v) {
        auto *p = (uint8_t *)&v;
        out.insert(out.end(), p, p + sizeof(v));
    };

    for (auto id : game_mode_->entities().all_entities()) {
        auto *ep = game_mode_->entities().get_component<Position>(id);
        auto *ec = game_mode_->entities().get_component<CombatStats>(id);
        if (!ep || !ec)
            continue;
        float x = ep->world_pos.x, y = ep->world_pos.y;
        int hp = ec->hp, max_hp = ec->max_hp;
        uint8_t alive = ec->alive ? 1 : 0;
        uint8_t team = static_cast<uint8_t>(ec->team);
        push(id);
        push(x);
        push(y);
        push(hp);
        push(max_hp);
        out.push_back(alive);
        out.push_back(team);
        uint8_t flags =
            game_mode_->entities().get_component<Interactable>(id) ? 1 : 0;
        out.push_back(flags);
    }
    return out;
}
