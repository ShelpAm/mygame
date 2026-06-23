#include "net/client.hpp"
#include "animation/animation-data.hpp"
#include "core/app.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/vision.hpp"
#include "net/net-packet.hpp"
#include "net/sync-io.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/formation.hpp"
#include "world/map-data.hpp"
#include <cassert>
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>

Client::Client(App *app) : app_(app), messages_(Session::io(), 128)
{
    spdlog::info("Client: initialized");
}

Client::~Client()
{
    spdlog::info("Client: destructing this");
}

awaitable<void> Client::attach_transport(std::shared_ptr<Session> t)
{
    spdlog::debug("Client: attaching transport {}", t->remote_info());

    if (!co_await authenticate_transport(t))
        throw std::runtime_error("Server verification failed: unexpected response");

    spdlog::info("Client: auth successful ({})", t->remote_info());

    session_ = t;
    spdlog::info("Client: transport ({}) attached", t->remote_info());

    // Note to ensure that `c` should outlive this coro.
    auto reading_loop = [](Client *c, std::shared_ptr<Session> t) -> awaitable<void> {
        try {
            while (true) {
                auto msg = co_await t->read();
                spdlog::log((msg.type == NetPacket::state_delta ? spdlog::level::trace
                                                                : spdlog::level::debug),
                            "Client: received message {} with payload size {} "
                            "from transport {}",
                            msg.type, msg.payload.size(), t->remote_info());
                if (!c->messages_.try_send(boost::system::error_code{}, t, msg))
                    Session::spawn([](Client *c, auto t, auto msg) -> awaitable<void> {
                        co_await c->messages_.async_send(boost::system::error_code{}, t,
                                                         std::move(msg));
                    }(c, t, std::move(msg)));
            }
        }
        catch (boost::system::system_error const &e) {
            // If passive, notify. If active, transport_guard_ may be guarding
            // others.
            c->detach_transport(detach_token{}, t.get());
            if (e.code() == asio::error::operation_aborted || e.code() == asio::error::eof ||
                e.code() == asio::experimental::error::channel_closed ||
                e.code() == asio::experimental::error::channel_cancelled) {
                spdlog::debug("Client: transport {} closed because {}", t->remote_info(), e.what());
                co_return; // Normal exits
            }
            throw;
        }
    };
    Session::spawn(reading_loop(this, t));
}

void Client::detach_transport(detach_token, Session *s)
{
    spdlog::info("Client: transport {} detached", s->remote_info());
    // Don't reset here, because this maybe no longer that session.
    // session_.reset();

    player_id_ = invalid_entity;
    remote_entities_.clear();
    combat_events_.clear();
    chat_history_.clear();
}

void Client::send_join_request()
{
    if (!session_ || !session_->is_open())
        throw std::runtime_error("send_join_request: transport not attached or closed");
    Session::spawn([](std::shared_ptr<Session> t) -> awaitable<void> {
        co_await t->write({NetPacket::join, std::vector<std::uint8_t>{}});
    }(session_));
}

void Client::send_player_direction(Vec2f dir)
{
    if (!session_ || !session_->is_open()) {
        throw std::runtime_error("send_player_direction: transport not attached or closed");
    }
    spdlog::trace("Client: sending move dir: {}, {}", dir.x, dir.y);
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        co_await t->write({NetPacket::player_input, payload});
    }(session_, make_player_input(player_id_, dir.x, dir.y)));
}

void Client::send_recruit()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::recruit_soldier, payload});
        }(session_, make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_recruit_ranged()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::recruit_ranged, payload});
        }(session_, make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_soldier_command()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::soldier_command, payload});
        }(session_, make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_respawn()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::respawn, payload});
        }(session_, make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_cycle_formation()
{
    assert(session_);
    int n = (int)formation_registry().size();
    formation_idx_ = (formation_idx_ + 1) % n;
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::formation, payload});
        }(session_, make_formation_payload(player_id_, selected_roles_)),
        detached);
}

void Client::send_interact()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::interact, payload});
        }(session_, make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_rest()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::rest, payload});
        }(session_, make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_chat(std::string const &msg)
{
    assert(session_);
    chat_history_.push_back("You: " + msg);
    std::vector<uint8_t> p(msg.begin(), msg.end());
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        co_await t->write({NetPacket::chat, std::move(payload)});
    }(session_, std::move(p)));
}

void Client::send_dialogue_action(std::string const &action)
{
    assert(session_);
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        co_await t->write({NetPacket::dialogue_action, std::move(payload)});
    }(session_, std::vector<uint8_t>(action.begin(), action.end())));
}

void Client::handle_message(Session &from, TransportMessage msg)
{
    // Quick handlers: tiny writes that don't touch remote_entities_
    if (msg.type == NetPacket::return_pid) {
        assert(msg.payload.size() >= 9);
        memcpy(&player_id_, msg.payload.data(), 8);
        player_team_ = static_cast<Team>(msg.payload[8]);
        spdlog::info("Client: Returned player ID from server: {} team: {}", player_id_,
                     player_team_);
        return;
    }
    if (msg.type == NetPacket::chat) {
        chat_history_.push_back(std::string(msg.payload.begin(), msg.payload.end()));
        return;
    }
    if (msg.type == NetPacket::dialogue_sync) {
        handle_dialogue_sync(msg.payload);
        return;
    }

    switch (msg.type) {
    case NetPacket::state_full:
        apply_sync_full(msg.payload);
        break;
    case NetPacket::state_delta:
        apply_sync_delta(msg.payload);
        break;
    case NetPacket::entity_removed:
        if (msg.payload.size() >= 8) {
            EntityId eid;
            memcpy(&eid, msg.payload.data(), 8);
            std::erase_if(remote_entities_, [eid](RemoteEntity const &re) { return re.id == eid; });
        }
        break;
    case NetPacket::kicked: {
        std::string reason(msg.payload.begin(), msg.payload.end());
        spdlog::info("Client: kicked by server: {}", reason);
        session_->close();
        app_->start_local_session();
        break;
    }
    case NetPacket::entity_update:
        handle_entity_update({msg.type, msg.payload});
        break;
    case NetPacket::combat_event: {
        auto ev = parse_combat_event(msg.payload);
        handle_combat_event(ev.attacker_id, ev.defender_id, ev.damage, ev.killed);
        break;
    }
    case NetPacket::projectile_fired: {
        if (msg.payload.size() >= 16) {
            ProjectileVisual pv;
            memcpy(&pv.pos.x, msg.payload.data(), 4);
            memcpy(&pv.pos.y, msg.payload.data() + 4, 4);
            memcpy(&pv.dst.x, msg.payload.data() + 8, 4);
            memcpy(&pv.dst.y, msg.payload.data() + 12, 4);
            Vec2f d = pv.dst - pv.pos;
            pv.total_dist = std::sqrt(d.x * d.x + d.y * d.y);
            pv.dir = pv.total_dist > 0.f ? Vec2f{d.x / pv.total_dist, d.y / pv.total_dist}
                                         : Vec2f{1.f, 0.f};
            projectile_visuals_.push_back(pv);
        }
        break;
    }
    default:
        break;
    }
}

void Client::handle_entity_update(NetPacket const &pkt)
{
    if (pkt.payload.size() < 25)
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
    RemoteEntity re;
    re.id = u.id;
    re.position = {u.x, u.y};
    re.target_pos = {u.x, u.y};
    re.hp = u.hp;
    re.max_hp = u.max_hp;
    re.alive = u.alive;
    remote_entities_.push_back(re);
}

void Client::update(float dt)
{
    while (messages_.try_receive(
        [this](boost::system::error_code, std::shared_ptr<Session> t, TransportMessage msg) {
            handle_message(*t, std::move(msg));
        })) {
    }

    if (player_id_ != invalid_entity) {
        interpolate_entities(dt);
        player_visibility_.set_visible_arc(world_to_tile(player_pos_), player_vision_range_,
                                           player_facing_, player_vision_arc_);

        // Manage snapshots based on tile visibility
        // auto const &visible = player_visibility_.visible;

        // Create/update snapshots for entities not on visible tiles
        // (entities in remote_entities_ were synced when visible, so their
        //  tiles are implicitly explored)
        // for (auto &re : remote_entities_) {
        //     auto tile = world_to_tile(re.position);
        //     if (!visible.contains(tile)) {
        //         auto it = std::find_if(
        //             snapshots_.begin(), snapshots_.end(),
        //             [&tile](SnapshotEntity const &s) {
        //                 return world_to_tile(s.position) == tile;
        //             });
        //         SnapshotEntity *sn = nullptr;
        //         if (it != snapshots_.end()) {
        //             sn = &*it;
        //         } else {
        //             snapshots_.emplace_back();
        //             sn = &snapshots_.back();
        //         }
        //         sn->position = re.position;
        //         sn->kind = re.kind;
        //         sn->facing = re.facing;
        //         sn->color = re.color;
        //         sn->scale = re.scale;
        //         sn->team = re.team;
        //         sn->alive = re.alive;
        //         sn->texture_name = re.texture_name;
        //     }
        // }

        for (auto &re : remote_entities_) {
            auto &clips = animation_clips_for_kind(re.kind, (uint8_t)re.team);
            auto *clip = determine_clip(clips, re.anim_state, re.velocity, re.alive, dt);
            switch_clip(re.anim_state, clip);
            tick_animation(re.anim_state, re.velocity, dt);
        }
    }

    // Tick projectile visuals
    for (auto it = projectile_visuals_.begin(); it != projectile_visuals_.end();) {
        float step = it->speed * dt;
        it->pos = it->pos + it->dir * step;
        it->traveled += step;
        if (it->traveled >= it->total_dist)
            it = projectile_visuals_.erase(it);
        else
            ++it;
    }
}

Vec2f Client::player_position() const
{
    if (player_id_ == invalid_entity)
        throw std::runtime_error("player_position: player ID not set");
    return player_pos_;
}

bool Client::is_player_dead()
{
    if (player_id_ == invalid_entity)
        throw std::runtime_error("is_player_dead: player ID not set");
    return !player_alive_;
}

CombatStats const *Client::player_stats()
{
    if (player_id_ == invalid_entity)
        return nullptr;
    static CombatStats stats;
    stats.hp = player_hp_;
    stats.max_hp = player_max_hp_;
    stats.alive = player_alive_;
    stats.attack = player_attack_;
    stats.defense = player_defense_;
    stats.attack_range = player_attack_range_;
    return &stats;
}

RemoteEntity *Client::find_entity(EntityId id)
{
    for (auto &re : remote_entities_)
        if (re.id == id)
            return &re;
    return nullptr;
}

static void set_visual_from_kind(RemoteEntity &re)
{
    switch (re.kind) {
    case EntityKind::player:
        re.color = {0.3f, 0.8f, 0.3f, 1.f};
        re.scale = 1.f;
        re.texture_name = "player";
        break;
    case EntityKind::soldier:
        re.color = re.team == Team::enemy ? SDL_FColor{0.8f, 0.3f, 0.1f, 1.f}
                                          : SDL_FColor{0.3f, 0.5f, 0.9f, 1.f};
        re.scale = 0.8f;
        re.texture_name = re.team == Team::enemy ? "enemy_soldier" : "soldier";
        break;
    case EntityKind::npc:
        re.color = {0.8f, 0.6f, 0.2f, 1.f};
        re.scale = 1.f;
        re.texture_name = "npc";
        break;
    case EntityKind::enemy:
        re.color = {0.9f, 0.2f, 0.1f, 1.f};
        re.scale = 1.f;
        re.texture_name = "enemy";
        break;
    case EntityKind::structure:
        re.color = {0.5f, 0.5f, 0.5f, 1.f};
        re.scale = 1.2f;
        re.texture_name = "structure";
        break;
    default:
        re.color = {0.3f, 0.5f, 0.9f, 1.f};
        re.scale = 0.8f;
        re.texture_name = "entity";
        break;
    }
    re.color.a = 1.f;
}

namespace {

// Per-entity parse result — RemoteEntity plus optional player-only data.
struct ParsedEntity {
    RemoteEntity re;
    int attack = 0;
    int defense = 0;
    float attack_range = 80.f;
    SurvivalState survival; // only valid when has_survival is set
    bool has_survival = false;
};

// Parse a single entity from the bitmask stream.
std::optional<ParsedEntity> parse_one_entity(SyncReader &r)
{
    if (r.remaining() < 10) // id(8) + mask(2)
        return std::nullopt;
    ParsedEntity pe;
    auto &re = pe.re;
    re.id = r.read<uint64_t>();
    uint16_t mask = r.read<uint16_t>();

    if (mask & SyncComponent::entity_kind)
        re.kind = r.read<uint8_t>();
    if (mask & SyncComponent::position) {
        Transform pos;
        pos.read_sync(r);
        re.target_pos = pos.world_pos;
        re.facing = pos.facing;
    }
    if (mask & SyncComponent::combat) {
        CombatStats cs;
        cs.read_sync(r);
        re.hp = cs.hp;
        re.max_hp = cs.max_hp;
        re.alive = cs.alive;
        re.team = cs.team;
        pe.attack = cs.attack;
        pe.defense = cs.defense;
        pe.attack_range = cs.attack_range;
    }
    if (mask & SyncComponent::movement) {
        Movement mov;
        mov.read_sync(r);
        re.velocity = mov.velocity;
    }
    if (mask & SyncComponent::soldier_ai) {
        SoldierAI ai;
        ai.read_sync(r);
        re.soldier_stance = ai.stance;
        re.soldier_role = ai.role;
    }
    if (mask & SyncComponent::interact)
        re.interactable = r.read<uint8_t>();
    if (mask & SyncComponent::survival) {
        pe.survival.read_sync(r);
        pe.has_survival = true;
    }
    if (mask & SyncComponent::vision) {
        Vision v;
        v.read_sync(r);
        re.vision_range = v.range;
        re.vision_arc = v.arc;
    }

    set_visual_from_kind(re);
    return pe;
}

} // namespace

// Skip the 9-byte world-state header: day(4), season(1), time_of_day(4)
static void parse_world_header(SyncReader &r, WorldState &ws)
{
    if (r.remaining() < 9)
        return;
    ws.set_day(r.read<int32_t>());
    ws.set_season(r.read<uint8_t>());
    ws.set_time_of_day(r.read<float>());
}

void Client::apply_sync_full(std::vector<uint8_t> const &data)
{
    SyncReader r{data.data(), data.size()};

    parse_world_header(r, world_state_);

    // Parse explored tiles (server-authoritative)
    {
        auto count = r.read<uint16_t>();
        for (uint16_t i = 0; i < count; ++i)
            player_visibility_.explore_single(Vec2i(r.read<int32_t>(), r.read<int32_t>()));
    }

    while (!r.done()) {
        auto opt = parse_one_entity(r);
        if (!opt)
            break;
        auto &pe = *opt;
        auto &re = pe.re;

        if (re.id == player_id_) {
            player_target_pos_ = re.target_pos;
            player_hp_ = re.hp;
            player_max_hp_ = re.max_hp;
            player_alive_ = re.alive;
            player_velocity_ = re.velocity;
            player_facing_ = re.facing;
            player_team_ = re.team;
            player_attack_ = pe.attack;
            player_defense_ = pe.defense;
            player_attack_range_ = pe.attack_range;
            player_vision_range_ = re.vision_range;
            player_vision_arc_ = re.vision_arc;
            if (pe.has_survival)
                player_survival_ = pe.survival;
        }

        // Upsert into remote_entities
        auto *rp = find_entity(re.id);
        if (!rp) {
            re.position = re.target_pos;
            remote_entities_.push_back(re);
        }
        else {
            rp->kind = re.kind;
            rp->target_pos = re.target_pos;
            rp->hp = re.hp;
            rp->max_hp = re.max_hp;
            rp->alive = re.alive;
            rp->team = re.team;
            rp->velocity = re.velocity;
            rp->facing = re.facing;
            rp->interactable = re.interactable;
            set_visual_from_kind(*rp);
        }
    }
    // Entities not in this sync stay — snapshot management is handled
    // per-frame in update() based on tile visibility
}

void Client::apply_sync_delta(std::vector<uint8_t> const &data)
{
    SyncReader r{data.data(), data.size()};

    parse_world_header(r, world_state_);

    // Parse explored tiles (server-authoritative)
    {
        auto count = r.read<uint16_t>();
        for (uint16_t i = 0; i < count; ++i) {
            auto x = r.read<int32_t>();
            auto y = r.read<int32_t>();
            player_visibility_.explore_single({x, y});
        }
    }

    while (!r.done()) {
        auto opt = parse_one_entity(r);
        if (!opt)
            break;
        auto &pe = *opt;
        auto &re = pe.re;

        if (re.id == player_id_) {
            player_target_pos_ = re.target_pos;
            player_hp_ = re.hp;
            player_max_hp_ = re.max_hp;
            player_alive_ = re.alive;
            player_velocity_ = re.velocity;
            player_facing_ = re.facing;
            player_team_ = re.team;
            player_attack_ = pe.attack;
            player_defense_ = pe.defense;
            player_attack_range_ = pe.attack_range;
            player_vision_range_ = re.vision_range;
            player_vision_arc_ = re.vision_arc;
            if (pe.has_survival)
                player_survival_ = pe.survival;
        }

        auto *rp = find_entity(re.id);
        if (!rp) {
            re.position = re.target_pos;
            remote_entities_.push_back(std::move(re));
        }
        else {
            rp->kind = re.kind;
            rp->target_pos = re.target_pos;
            rp->hp = re.hp;
            rp->max_hp = re.max_hp;
            rp->alive = re.alive;
            rp->team = re.team;
            rp->velocity = re.velocity;
            rp->facing = re.facing;
            rp->interactable = re.interactable;
            set_visual_from_kind(*rp);
        }
    }
}

void Client::handle_combat_event(EntityId attacker_id, EntityId defender_id, int damage,
                                 bool killed)
{
    EntityId target = (defender_id == 0) ? player_id_ : defender_id;
    if (target == invalid_entity) {
        spdlog::warn("handle_combat_event: target entity is invalid");
        return;
    }

    if (target == player_id_) {
        player_hp_ -= damage;
        if (killed)
            player_alive_ = false;
    }
    else {
        auto *rp = find_entity(target);
        if (!rp) {
            spdlog::warn("handle_combat_event: entity {} not found", target);
            return;
        }
        rp->hp -= damage;
        if (killed)
            rp->alive = false;
        rp->anim_state.hurt_timer = 0.3f;
    }

    // Trigger attack animation on attacker
    if (attacker_id != 0) {
        auto *atk = find_entity(attacker_id);
        if (atk)
            atk->anim_state.attack_timer = 0.3f;
    }

    combat_events_.push_back({attacker_id, defender_id, damage, killed});
}

void Client::interpolate_entities(float dt)
{
    float t = std::min(1.f, dt * 30.f);
    player_pos_.x += (player_target_pos_.x - player_pos_.x) * t;
    player_pos_.y += (player_target_pos_.y - player_pos_.y) * t;

    for (auto &e : remote_entities_) {
        e.position.x += (e.target_pos.x - e.position.x) * t;
        e.position.y += (e.target_pos.y - e.position.y) * t;
    }
}

void Client::handle_dialogue_sync(std::vector<uint8_t> const &data)
{
    if (data.size() < 4)
        throw std::runtime_error("handle_dialogue_sync: data too short, maybe corrupted");
    uint32_t name_len;
    memcpy(&name_len, data.data(), 4);
    if (name_len == 0) {
        dialogue_ = {};
        return;
    }
    auto s = parse_dialogue_sync({data.begin() + 4, data.end()});
    dialogue_.active = true;
    dialogue_.npc_name = std::move(s.npc_name);
    dialogue_.npc_trust = s.npc_trust;
    dialogue_.can_gift = s.can_gift;
    dialogue_.can_threaten = s.can_threaten;
    dialogue_.history.clear();
    for (auto &l : s.lines) {
        dialogue_.history.push_back({l.speaker == 0 ? DialogueLine::player : DialogueLine::npc,
                                     l.use_raw ? "" : l.text, l.use_raw ? l.text : "", l.use_raw,
                                     std::move(l.npc_name)});
    }
    dialogue_.available_topics = std::move(s.topics);
    dialogue_.available_actions = std::move(s.actions);
}

awaitable<bool> Client::authenticate_transport(std::shared_ptr<Session> t)
{
    try {
        spdlog::debug("Client: sending auth: payload: {}", auth_payload());
        co_await t->write({NetPacket::auth, auth_payload()});
        auto res = co_await t->read();
        spdlog::debug("Client: auth result: type: {}, payload: {}", res.type, res.payload);
        co_return res.type == NetPacket::auth &&res.payload == auth_payload();
    }
    catch (...) {
        co_return false;
    }
}
