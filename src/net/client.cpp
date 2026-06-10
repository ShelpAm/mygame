#include "net/client.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
#include "net/net-packet.hpp"
#include "net/network-manager.hpp"
#include "net/network-transport.hpp"
#include <cstring>
#include <print>

Client::Client() = default;

void Client::attach_local(ITransport *t)
{
    transport_.reset(t);
    transport_->set_callback(std::bind_front(&Client::on_message, this));
}

void Client::attach_network(NetworkManager &net)
{
    transport_ = std::make_unique<NetworkTransport>(net);
    transport_->set_callback(std::bind_front(&Client::on_message, this));
}

void Client::detach_transport()
{
    transport_.reset();
    id_map_.clear();
    player_id_ = invalid_entity;
}

void Client::reset()
{
    for (auto eid : em_.all_entities())
        em_.destroy_entity(eid);
    id_map_.clear();
    player_id_ = invalid_entity;
}

void Client::send_join_request()
{
    assert(!transport_);
    transport_->send({NetPacket::join, std::vector<std::uint8_t>{}}); // Empty
}

void Client::send_player_direction(Vec2f dir)
{
    if (!transport_)
        return;
    std::vector<uint8_t> payload;
    write_bytes(payload, player_id_); // player_id
    write_float(payload, dir.x);
    write_float(payload, dir.y);
    transport_->send({NetPacket::player_input, std::move(payload)});
}

void Client::send_recruit()
{
    if (!transport_)
        return;
    std::vector<uint8_t> payload;
    write_bytes(payload, player_id_);
    transport_->send({NetPacket::recruit_soldier, std::move(payload)});
}

void Client::send_interact()
{
    if (!transport_)
        return;
    std::vector<uint8_t> payload;
    write_bytes(payload, player_id_); // player_id
    transport_->send({NetPacket::interact, std::move(payload)});
}

void Client::send_rest()
{
    if (!transport_)
        return;
    std::vector<uint8_t> payload;
    write_bytes(payload, player_id_); // player_id
    transport_->send({NetPacket::rest, std::move(payload)});
}

void Client::on_message(TransportExMessage const &msg)
{
    if (msg.type == 1 /* state_full */) {
        apply_sync(msg.payload);
    }
    else if (msg.type == 5 /* combat_event */ && msg.payload.size() >= 13) {
        int att, def, dmg;
        uint8_t k;
        memcpy(&att, msg.payload.data(), 4);
        memcpy(&def, msg.payload.data() + 4, 4);
        memcpy(&dmg, msg.payload.data() + 8, 4);
        k = msg.payload[12];
        handle_combat_event(att, def, dmg, (bool)k);
    }
    else if (msg.type == NetPacket::return_pid) {
        memcpy(&player_id_, msg.payload.data(), 4);
        std::println("Returned player ID: {}", player_id_);
    }
}

void Client::update(float dt)
{
    (void)dt;
    if (transport_)
        transport_->do_receive();
}

EntityId Client::local_player() const
{
    return player_id_;
}

Vec2f Client::player_position()
{
    if (player_id_ == invalid_entity)
        return {};
    auto *p = em_.get_component<Position>(player_id_);
    return p ? p->world_pos : Vec2f{};
}

bool Client::is_player_dead()
{
    if (player_id_ == invalid_entity)
        return false;
    auto *cs = em_.get_component<CombatStats>(player_id_);
    return cs && !cs->alive;
}

CombatStats const *Client::player_stats()
{
    if (player_id_ == invalid_entity)
        return nullptr;
    return em_.get_component<CombatStats>(player_id_);
}

void Client::apply_sync(std::vector<uint8_t> const &data)
{
    for (size_t i = 0; i + 22 <= data.size(); i += 22) {
        auto readInt = [&](size_t off) {
            int v;
            memcpy(&v, data.data() + i + off, 4);
            return v;
        };
        auto read_float = [&](size_t off) {
            float v;
            memcpy(&v, data.data() + i + off, 4);
            return v;
        };
        int nid = readInt(0);
        float x = read_float(4), y = read_float(8);
        int hp = readInt(12), max_hp = readInt(16);
        bool alive = data[i + 20] != 0;
        int team = data[i + 21];

        if (nid == 0) {
            // Host player — create or update from server sync
            if (player_id_ == invalid_entity) {
                player_id_ = em_.create_entity();
                em_.add_component<Position>(player_id_,
                                            Position{{x, y}, {0, 0}, 1.f});
                em_.add_component<Sprite>(player_id_,
                                          Sprite{"player",
                                                 {},
                                                 {16, 16},
                                                 {0.3f, 0.8f, 0.3f, 1.f},
                                                 1.f,
                                                 true});
                em_.add_component<CombatStats>(
                    player_id_,
                    CombatStats{Team::player, max_hp, hp, 4, 3, 80.f});
            }
            else {
                auto *p = em_.get_component<Position>(player_id_);
                auto *c = em_.get_component<CombatStats>(player_id_);
                if (p)
                    p->world_pos = {x, y};
                if (c) {
                    c->hp = hp;
                    c->max_hp = max_hp;
                    c->alive = alive;
                }
            }
            continue;
        }

        auto it = id_map_.find(nid);
        if (it == id_map_.end()) {
            auto eid = em_.create_entity();
            id_map_[nid] = eid;
            em_.add_component<Position>(eid, Position{{x, y}, {0, 0}, 0.5f});
            SDL_FColor color;
            if (team == 1)
                color = {0.8f, 0.2f, 0.2f, 1.f}; // Enemy: red
            else if (team == 2)
                color = {0.8f, 0.6f, 0.2f, 1.f}; // Neutral: gold
            else
                color = {0.3f, 0.5f, 0.9f, 1.f}; // Player/friendly: blue
            em_.add_component<Sprite>(
                eid, Sprite{"", {}, {12, 12}, color, 0.8f, true});
            em_.add_component<CombatStats>(
                eid, CombatStats{
                         team == 1 ? Team::enemy
                                   : (team == 2 ? Team::neutral : Team::player),
                         max_hp, hp, 3, 2, 80.f});
        }
        else {
            auto eid = it->second;
            auto *p = em_.get_component<Position>(eid);
            auto *c = em_.get_component<CombatStats>(eid);
            auto *s = em_.get_component<Sprite>(eid);
            if (p)
                p->world_pos = {x, y};
            if (c) {
                c->hp = hp;
                c->alive = alive;
            }
            if (s) {
                if (team == 1)
                    s->color = {0.8f, 0.2f, 0.2f, 1.f};
                else if (team == 2)
                    s->color = {0.8f, 0.6f, 0.2f, 1.f};
                else
                    s->color = {0.3f, 0.5f, 0.9f, 1.f};
            }
        }
    }
}

void Client::handle_combat_event(int attacker_id, int defender_id, int damage,
                                 bool killed)
{
    (void)attacker_id;
    if (defender_id == 0) {
        if (player_id_ != invalid_entity) {
            auto *cs = em_.get_component<CombatStats>(player_id_);
            if (cs) {
                cs->hp -= damage;
                if (killed)
                    cs->alive = false;
            }
        }
        return;
    }
    auto it = id_map_.find(defender_id);
    EntityId eid = (it != id_map_.end()) ? it->second : invalid_entity;
    auto *cs = em_.get_component<CombatStats>(eid);
    if (cs) {
        cs->hp -= damage;
        if (killed)
            cs->alive = false;
    }
}
