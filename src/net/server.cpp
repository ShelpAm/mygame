#include "net/server.hpp"
#include "core/game-mode.hpp"
#include "factions/event-simulator.hpp"
#include "net/client.hpp"
#include "net/local-transport.hpp"
#include "net/network-manager.hpp"
#include "net/network-transport.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/combat-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"
#include <cstring>
#include <print>

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

EntityId Server::spawn_player(int id)
{
    (void)id;
    return em_.create_entity();
}

void Server::attach_local_pair(Client &client)
{
    auto [srv, cli] = create_transport_pair();
    srv->set_callback(
        [this](TransportExMessage const &msg) { on_message(msg); });
    client.attach_local(cli.release());
    transports_.push_back(std::move(srv));
}

void Server::attach_network(NetworkManager &net)
{
    auto t = std::make_unique<NetworkTransport>(net);
    t->set_callback([this](TransportExMessage const &msg) { on_message(msg); });
    transports_.push_back(std::move(t));
}

void Server::clear_transports()
{
    transports_.clear();
}

void Server::on_message(TransportExMessage const &msg)
{
    if (msg.type == NetPacket::join) {
        assert(msg.payload.empty());
        auto eid = add_player({0, 0});
        std::vector<uint8_t> payload;
        write_bytes(payload, eid); // player_id
        msg.from->send({NetPacket::return_pid, payload});
        std::println("New player joined with ID: {}", eid);
    }
    else if (msg.type == NetPacket::entity_update) {
        assert(msg.payload.size() >= 21);
        int id;
        float x, y;
        int hp, max_hp;
        uint8_t alive;
        memcpy(&id, msg.payload.data(), 4);
        memcpy(&x, msg.payload.data() + 4, 4);
        memcpy(&y, msg.payload.data() + 8, 4);
        memcpy(&hp, msg.payload.data() + 12, 4);
        memcpy(&max_hp, msg.payload.data() + 16, 4);
        alive = msg.payload[20];

        assert(player_entities_.contains(id));
        update_player(id, {x, y}, hp, max_hp, (bool)alive);
    }
    else if (msg.type == NetPacket::recruit_soldier) {
        assert(msg.payload.size() >= 4);
        uint32_t pid;
        memcpy(&pid, msg.payload.data(), 4);
        assert(leader != invalid_entity);
        auto eid = game_mode_->spawn_soldier(pid, soldier_idx_++, {0, -1});
        auto *lp = em_.get_component<Position>(pid);
        auto *sp = em_.get_component<Position>(eid);
        assert(lp && sp);
        sp->world_pos = {lp->world_pos.x + 32.f, lp->world_pos.y + 32.f};
    }
    else if (msg.type == NetPacket::spawn_enemy_wave) {
        assert(msg.payload.size() >= 13);
        float cx, cy;
        int cnt;
        uint8_t tm;
        memcpy(&cx, msg.payload.data(), 4);
        memcpy(&cy, msg.payload.data() + 4, 4);
        memcpy(&cnt, msg.payload.data() + 8, 4);
        tm = msg.payload[12];
        assert(cs_);
        cs_->spawn_enemy_wave(em_, cnt, {cx, cy}, 400.f, (Team)tm);
    }
    else if (msg.type == NetPacket::player_input) {
        assert(msg.payload.size() >= 12);
        EntityId pid;
        float mx, my;
        memcpy(&pid, msg.payload.data(), 4);
        memcpy(&mx, msg.payload.data() + 4, 4);
        memcpy(&my, msg.payload.data() + 8, 4);

        pending_inputs_.push({pid, mx, my});
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
        auto *cs = em_.get_component<CombatStats>(pid);
        if (cs && cs->alive) {
            cs->hp = std::min(cs->max_hp, cs->hp + 5);
            if (survival_)
                survival_->heal(5.f);
            mark_needs_full_sync();
        }
    }
    else if (msg.type == NetPacket::combat_event) {
        assert(msg.payload.size() >= 13);
        int att, def, dmg;
        uint8_t k;
        memcpy(&att, msg.payload.data(), 4);
        memcpy(&def, msg.payload.data() + 4, 4);
        memcpy(&dmg, msg.payload.data() + 8, 4);
        k = msg.payload[12];
        handle_combat_event(att, def, dmg, (bool)k);
    }
}

void Server::update(float dt)
{
    if (!cs_)
        return;

    for (auto &t : transports_)
        t->do_receive();

    // Processes user input
    while (!pending_inputs_.empty()) {
        auto const &[pid, mx, my] = pending_inputs_.front();
        assert(pid != invalid_entity);
        auto *pp = em_.get_component<Position>(pid);
        assert(pp);
        assert(game_mode_);

        Vec2f new_pos{pp->world_pos.x + mx * 200.f * dt,
                      pp->world_pos.y + my * 200.f * dt};
        game_mode_->apply_player_movement(pid, new_pos);
        pending_inputs_.pop();
    }

    cs_->update(em_, dt);
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
            t->send({NetPacket::combat_event, p});
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
            auto *pos = em_.get_component<Position>(pid);
            Vec2f center = pos ? pos->world_pos : Vec2f{};
            auto &latest = events_->triggered_events().back();
            if (latest.type == GameEvent::Type::battle ||
                latest.type == GameEvent::Type::refugee_wave)
                cs_->spawn_enemy_wave(em_, 2 + rand() % 4, center, 400.f,
                                      Team::enemy);
        }
    }
}

void Server::broadcast_sync()
{
    auto payload = build_sync_payload();
    if (payload.empty())
        return;
    for (auto &t : transports_)
        t->send({NetPacket::state_full, payload});
}

void Server::handle_combat_event(int attacker_id, int defender_id, int damage,
                                 bool killed)
{
    (void)attacker_id;
    auto *cs = em_.get_component<CombatStats>(defender_id);
    if (cs) {
        cs->hp -= damage;
        if (killed)
            cs->alive = false;
    }
}

EntityId Server::add_player(Vec2f pos)
{
    auto eid = em_.create_entity();
    std::println("Adding player {} at position ({}, {})", eid, pos.x, pos.y);
    em_.add_component<Position>(eid, Position{pos, {0, 0}, 1.f});
    em_.add_component<CombatStats>(
        eid, CombatStats{Team::player, 20, 20, 4, 3, 80.f});
    mark_needs_full_sync();
    return eid;
}

void Server::update_player(EntityId player_id, Vec2f pos, int hp, int max_hp,
                           bool alive)
{
    auto *p = em_.get_component<Position>(player_id);
    auto *c = em_.get_component<CombatStats>(player_id);
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

    for (auto id : em_.all_entities()) {
        auto *ep = em_.get_component<Position>(id);
        auto *ec = em_.get_component<CombatStats>(id);
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
    }
    return out;
}
