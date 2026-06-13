#include "net/client.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/interactable.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
#include "net/net-packet.hpp"
#include <cassert>
#include <cstring>
#include <spdlog/spdlog.h>

Client::Client() = default;

void Client::attach_transport(std::unique_ptr<ITransport> t)
{
    transport_ = std::move(t);

    co_spawn(
        ITransport::io(),
        [this]() -> awaitable<void> {
            spdlog::info("Transport ({}) connected to server",
                         static_cast<void *>(transport_.get()));
            while (true) {
                try {
                    on_message(*transport_, co_await transport_->read());
                }
                catch (boost::system::system_error const &) {
                    break;
                }
            }
            spdlog::info("Transport ({}) disconnected from server",
                         static_cast<void *>(transport_.get()));
            co_return;
        },
        detached);
}

void Client::detach_transport()
{
    transport_.reset();
    id_map_.clear();
    player_id_ = invalid_entity;
}

void Client::reset()
{
    world_.each([](flecs::entity e) { e.destruct(); });
    id_map_.clear();
    player_id_ = invalid_entity;
    remote_entities_.clear();
    chat_history_.clear();
}

void Client::send_join_request()
{
    if (!transport_) {
        spdlog::error("send_join_request: no transport attached");
        return;
    }
    co_spawn(ITransport::io(),
             transport_->write({NetPacket::join, std::vector<std::uint8_t>{}}),
             detached);
}

void Client::send_player_direction(Vec2f dir)
{
    if (!transport_)
        throw std::runtime_error(
            "send_player_direction: no transport attached");
    std::vector<uint8_t> payload;
    write_bytes(payload, static_cast<uint32_t>(player_id_));
    write_float(payload, dir.x);
    write_float(payload, dir.y);
    spdlog::info("move dir: {}, {}", dir.x, dir.y);
    co_spawn(ITransport::io(),
             transport_->write({NetPacket::player_input, payload}), detached);
    co_spawn(ITransport::io(),
             transport_->write({NetPacket::player_input, payload}), detached);
    co_spawn(ITransport::io(),
             transport_->write({NetPacket::player_input, std::move(payload)}),
             detached);
}

void Client::send_recruit()
{
    if (!transport_) {
        spdlog::warn("send_recruit: no transport attached");
        return;
    }
    std::vector<uint8_t> payload;
    write_bytes(payload, static_cast<uint32_t>(player_id_));
    co_spawn(
        ITransport::io(),
        transport_->write({NetPacket::recruit_soldier, std::move(payload)}),
        detached);
}

void Client::send_interact()
{
    if (!transport_) {
        spdlog::warn("send_interact: no transport attached");
        return;
    }
    std::vector<uint8_t> payload;
    write_bytes(payload, static_cast<uint32_t>(player_id_));
    co_spawn(ITransport::io(),
             transport_->write({NetPacket::interact, std::move(payload)}),
             detached);
}

void Client::send_rest()
{
    if (!transport_) {
        spdlog::warn("send_rest: no transport attached");
        return;
    }
    std::vector<uint8_t> payload;
    write_bytes(payload, static_cast<uint32_t>(player_id_));
    co_spawn(ITransport::io(),
             transport_->write({NetPacket::rest, std::move(payload)}),
             detached);
}

void Client::send_chat(std::string const &msg)
{
    if (!transport_) {
        spdlog::warn("send_chat: no transport attached");
        return;
    }
    chat_history_.push_back("You: " + msg);
    std::vector<uint8_t> p(msg.begin(), msg.end());
    co_spawn(ITransport::io(),
             transport_->write({NetPacket::chat, std::move(p)}), detached);
}

void Client::send_dialogue_action(std::string const &action)
{
    if (!transport_) {
        spdlog::warn("send_dialogue_action: no transport attached");
        return;
    }
    co_spawn(
        ITransport::io(),
        transport_->write({NetPacket::dialogue_action,
                           std::vector<uint8_t>(action.begin(), action.end())}),
        detached);
}

void Client::on_message(ITransport &from, TransportMessage const &msg)
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
    case NetPacket::return_pid: {
        uint32_t pid;
        memcpy(&pid, msg.payload.data(), 4);
        player_id_ = pid;
        spdlog::info("Returned player ID: {}", player_id_);
        break;
    }
    case NetPacket::dialogue_sync:
        handle_dialogue_sync(msg.payload);
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

void Client::update(float dt) {}

EntityId Client::local_player() const
{
    return player_id_;
}

Vec2f Client::player_position()
{
    if (player_id_ == invalid_entity)
        return {};
    auto it = id_map_.find(static_cast<int>(player_id_));
    if (it == id_map_.end())
        return {};
    const auto *p = world_.try_get<Position>(it->second);
    return p ? p->world_pos : Vec2f{};
}

bool Client::is_player_dead()
{
    if (player_id_ == invalid_entity)
        return false;
    auto it = id_map_.find(static_cast<int>(player_id_));
    if (it == id_map_.end())
        return false;
    const auto *cs = world_.try_get<CombatStats>(it->second);
    return cs && !cs->alive;
}

CombatStats const *Client::player_stats()
{
    if (player_id_ == invalid_entity)
        return nullptr;
    auto it = id_map_.find(static_cast<int>(player_id_));
    if (it == id_map_.end())
        return nullptr;
    return world_.try_get<CombatStats>(it->second);
}

void Client::apply_sync(std::vector<uint8_t> const &data)
{
    for (size_t i = 0; i + 23 <= data.size(); i += 23) {
        auto se = parse_sync_entity(data, i);

        if (se.id == 0) {
            if (player_id_ == invalid_entity) {
                auto e = world_.entity();
                player_id_ = e.id();
                e.set<Position>(Position{{se.x, se.y}, {0, 0}, 1.f});
                e.set<Sprite>(Sprite{"player",
                                      {},
                                      {16, 16},
                                      {0.3f, 0.8f, 0.3f, 1.f},
                                      1.f,
                                      true});
                e.set<CombatStats>(
                    CombatStats{Team::player, se.max_hp, se.hp, 4, 3, 80.f});
            }
            else {
                auto *p =
                    world_.entity(player_id_).try_get_mut<Position>();
                auto *c =
                    world_.entity(player_id_).try_get_mut<CombatStats>();
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
            auto e = world_.entity();
            auto eid = e.id();
            id_map_[se.id] = eid;
            e.set<Position>(Position{{se.x, se.y}, {0, 0}, 0.5f});
            SDL_FColor color;
            if (se.team == 1)
                color = {0.8f, 0.2f, 0.2f, 1.f};
            else if (se.team == 2)
                color = {0.8f, 0.6f, 0.2f, 1.f};
            else
                color = {0.3f, 0.5f, 0.9f, 1.f};
            e.set<Sprite>(Sprite{"", {}, {12, 12}, color, 0.8f, true});
            e.set<CombatStats>(
                CombatStats{se.team == 1   ? Team::enemy
                            : (se.team == 2 ? Team::neutral
                                            : Team::player),
                            se.max_hp,
                            se.hp,
                            3,
                            2,
                            80.f});
            if (se.flags & 1)
                e.set<Interactable>(Interactable{64.f, true});
        }
        else {
            auto eid = it->second;
            auto *p = world_.entity(eid).try_get_mut<Position>();
            auto *c = world_.entity(eid).try_get_mut<CombatStats>();
            auto *s = world_.entity(eid).try_get_mut<Sprite>();
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
            auto it = id_map_.find(static_cast<int>(player_id_));
            if (it != id_map_.end()) {
                auto *cs = world_.entity(it->second).try_get_mut<CombatStats>();
                if (cs) {
                    cs->hp -= damage;
                    if (killed)
                        cs->alive = false;
                }
            }
        }
        return;
    }
    auto it = id_map_.find(defender_id);
    EntityId eid = (it != id_map_.end()) ? it->second : invalid_entity;
    auto *cs = world_.entity(eid).try_get_mut<CombatStats>();
    if (cs) {
        cs->hp -= damage;
        if (killed)
            cs->alive = false;
    }
}

void Client::handle_dialogue_sync(std::vector<uint8_t> const &data)
{
    if (data.size() < 4)
        return;
    uint32_t name_len;
    memcpy(&name_len, data.data(), 4);
    if (name_len == 0) {
        dialogue_ = {};
        return;
    }
    auto s = parse_dialogue_sync(data);
    dialogue_.active = true;
    dialogue_.npc_name = std::move(s.npc_name);
    dialogue_.npc_trust = s.npc_trust;
    dialogue_.can_gift = s.can_gift;
    dialogue_.can_threaten = s.can_threaten;
    dialogue_.history.clear();
    for (auto &l : s.lines) {
        dialogue_.history.push_back(
            {l.speaker == 0 ? DialogueLine::player : DialogueLine::npc,
             l.use_raw ? "" : l.text, l.use_raw ? l.text : "", l.use_raw,
             std::move(l.npc_name)});
    }
    dialogue_.available_topics = std::move(s.topics);
    dialogue_.available_actions = std::move(s.actions);
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
