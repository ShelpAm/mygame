#include "net/client.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
#include "net/net-packet.hpp"
#include <cassert>
#include <cstring>
#include <print>

Client::Client() = default;

void Client::attach_transport(std::unique_ptr<ITransport> t)
{
    transport_ = std::move(t);
    transport_->set_callback(
        [this](TransportExMessage const &msg) { on_message(msg); });
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
    remote_entities_.clear();
    chat_history_.clear();
}

void Client::send_join_request()
{
    assert(transport_);
    transport_->send({NetPacket::join, std::vector<std::uint8_t>{}});
}

void Client::send_player_direction(Vec2f dir)
{
    if (!transport_)
        return;
    std::vector<uint8_t> payload;
    write_bytes(payload, player_id_);
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
    write_bytes(payload, player_id_);
    transport_->send({NetPacket::interact, std::move(payload)});
}

void Client::send_rest()
{
    if (!transport_)
        return;
    std::vector<uint8_t> payload;
    write_bytes(payload, player_id_);
    transport_->send({NetPacket::rest, std::move(payload)});
}

void Client::send_chat(std::string const &msg)
{
    if (!transport_)
        return;
    chat_history_.push_back("You: " + msg);
    std::vector<uint8_t> p(msg.begin(), msg.end());
    transport_->send({NetPacket::chat, std::move(p)});
}

void Client::on_message(TransportExMessage const &msg)
{
    switch (msg.type) {
    case NetPacket::state_full:
        apply_sync(msg.payload);
        break;
    case NetPacket::entity_update:
        handle_entity_update({msg.type, msg.payload});
        break;
    case NetPacket::chat: {
        std::string text(msg.payload.begin(), msg.payload.end());
        chat_history_.push_back(text);
        break;
    }
    case NetPacket::combat_event: {
        auto ev = parse_combat_event(msg.payload);
        handle_combat_event(ev.attacker_id, ev.defender_id, ev.damage,
                            ev.killed);
        break;
    }
    case NetPacket::return_pid:
        memcpy(&player_id_, msg.payload.data(), 4);
        std::println("Returned player ID: {}", player_id_);
        break;
    default:
        break;
    }
}

void Client::handle_entity_update(NetPacket const &pkt)
{
    if (pkt.payload.size() < 21)
        return;
    auto u = parse_entity_update(pkt.payload);
    for (auto &rp : remote_entities_) {
        if (rp.id == u.id) {
            rp.target_pos = {u.x, u.y};
            rp.hp = u.hp;
            rp.max_hp = u.max_hp;
            rp.alive = u.alive;
            return;
        }
    }
    remote_entities_.push_back(
        {u.id, {u.x, u.y}, {u.x, u.y}, u.hp, u.max_hp, u.alive});
}

void Client::update(float dt)
{
    if (transport_)
        transport_->consume();
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
        auto se = parse_sync_entity(data, i);

        if (se.id == 0) {
            if (player_id_ == invalid_entity) {
                player_id_ = em_.create_entity();
                em_.add_component<Position>(
                    player_id_, Position{{se.x, se.y}, {0, 0}, 1.f});
                em_.add_component<Sprite>(player_id_,
                                          Sprite{"player",
                                                 {},
                                                 {16, 16},
                                                 {0.3f, 0.8f, 0.3f, 1.f},
                                                 1.f,
                                                 true});
                em_.add_component<CombatStats>(
                    player_id_,
                    CombatStats{Team::player, se.max_hp, se.hp, 4, 3, 80.f});
            }
            else {
                auto *p = em_.get_component<Position>(player_id_);
                auto *c = em_.get_component<CombatStats>(player_id_);
                if (p)
                    p->world_pos = {se.x, se.y};
                if (c) {
                    c->hp = se.hp;
                    c->max_hp = se.max_hp;
                    c->alive = se.alive;
                }
            }
            continue;
        }

        auto it = id_map_.find(se.id);
        if (it == id_map_.end()) {
            auto eid = em_.create_entity();
            id_map_[se.id] = eid;
            em_.add_component<Position>(eid,
                                        Position{{se.x, se.y}, {0, 0}, 0.5f});
            SDL_FColor color;
            if (se.team == 1)
                color = {0.8f, 0.2f, 0.2f, 1.f};
            else if (se.team == 2)
                color = {0.8f, 0.6f, 0.2f, 1.f};
            else
                color = {0.3f, 0.5f, 0.9f, 1.f};
            em_.add_component<Sprite>(
                eid, Sprite{"", {}, {12, 12}, color, 0.8f, true});
            em_.add_component<CombatStats>(
                eid, CombatStats{se.team == 1 ? Team::enemy
                                              : (se.team == 2 ? Team::neutral
                                                              : Team::player),
                                 se.max_hp, se.hp, 3, 2, 80.f});
        }
        else {
            auto eid = it->second;
            auto *p = em_.get_component<Position>(eid);
            auto *c = em_.get_component<CombatStats>(eid);
            auto *s = em_.get_component<Sprite>(eid);
            if (p)
                p->world_pos = {se.x, se.y};
            if (c) {
                c->hp = se.hp;
                c->alive = se.alive;
            }
            if (s) {
                if (se.team == 1)
                    s->color = {0.8f, 0.2f, 0.2f, 1.f};
                else if (se.team == 2)
                    s->color = {0.8f, 0.6f, 0.2f, 1.f};
                else
                    s->color = {0.3f, 0.5f, 0.9f, 1.f};
            }
        }

        // Also update remote_entities_ for UI
        bool found = false;
        for (auto &re : remote_entities_) {
            if (re.id == se.id) {
                re.target_pos = {se.x, se.y};
                re.hp = se.hp;
                re.max_hp = se.max_hp;
                re.alive = se.alive;
                re.team = se.team;
                found = true;
                break;
            }
        }
        if (!found)
            remote_entities_.push_back({se.id,
                                        {se.x, se.y},
                                        {se.x, se.y},
                                        se.hp,
                                        se.max_hp,
                                        se.alive,
                                        se.team});
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

void Client::interpolate_entities(float dt)
{
    for (auto &e : remote_entities_) {
        e.position.x +=
            (e.target_pos.x - e.position.x) * std::min(1.f, dt * 30.f);
        e.position.y +=
            (e.target_pos.y - e.position.y) * std::min(1.f, dt * 30.f);
    }
}
