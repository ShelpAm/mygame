#include "net/client.hpp"
#include "core/app.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "net/net-packet.hpp"
#include "net/sync-io.hpp"
#include "survival/condition-tracker.hpp"
#include <cassert>
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>

Client::Client(App *app) : app_(app), messages_(ITransport::io(), 128)
{
    spdlog::info("Client: initialized");
}

Client::~Client()
{
    spdlog::info("Client: destructing this");
}

awaitable<void> Client::attach_transport(std::shared_ptr<ITransport> t)
{
    spdlog::debug("Client: attaching transport {}", t->remote_info());

    if (!co_await authenticate_transport(t))
        throw std::runtime_error(
            "Server verification failed: unexpected response");

    spdlog::info("Client: auth successful ({})", t->remote_info());

    transport_guard_ = std::make_unique<TransportGuard>(t);
    spdlog::info("Client: transport ({}) attached", t->remote_info());

    // Note to ensure that `c` should outlive this coro.
    auto reading_loop = [](Client *c,
                           std::shared_ptr<ITransport> t) -> awaitable<void> {
        try {
            while (true) {
                auto msg = co_await t->read();
                spdlog::log((msg.type == NetPacket::state_delta
                                 ? spdlog::level::trace
                                 : spdlog::level::debug),
                            "Client: received message {} with payload size {} "
                            "from transport {}",
                            msg.type, msg.payload.size(), t->remote_info());
                if (!c->messages_.try_send(boost::system::error_code{}, t, msg))
                    co_await c->messages_.async_send(
                        boost::system::error_code{}, t, std::move(msg));
            }
        }
        catch (boost::system::system_error const &e) {
            c->detach_transport();
            if (e.code() == asio::error::operation_aborted ||
                e.code() == asio::error::eof ||
                e.code() == asio::experimental::error::channel_closed ||
                e.code() == asio::experimental::error::channel_cancelled) {
                co_return; // Normal exits
            }
            throw;
        }
    };
    ITransport::spawn(reading_loop(this, t));
}

void Client::detach_transport()
{
    if (transport_guard_) {
        spdlog::info("Client: transport {} detached",
                     transport_guard_->get()->remote_info());
        transport_guard_.reset(); // Guards transport to close.
    }
    else {
        spdlog::info(
            "Client: detach_transport called but no transport attached");
    }

    player_id_ = invalid_entity;
    remote_entities_.clear();
    combat_events_.clear();
    chat_history_.clear();
}

void Client::send_join_request()
{
    if (!transport_guard_ || !transport_guard_->get()->is_open())
        throw std::runtime_error(
            "send_join_request: transport not attached or closed");
    ITransport::spawn([](std::shared_ptr<ITransport> t) -> awaitable<void> {
        co_await t->write({NetPacket::join, std::vector<std::uint8_t>{}});
    }(transport_guard_->get()));
}

void Client::send_player_direction(Vec2f dir)
{
    if (!transport_guard_ || !transport_guard_->get()->is_open()) {
        throw std::runtime_error(
            "send_player_direction: transport not attached or closed");
    }
    spdlog::debug("Client: sending move dir: {}, {}", dir.x, dir.y);
    ITransport::spawn([](std::shared_ptr<ITransport> t,
                         auto payload) -> awaitable<void> {
        co_await t->write({NetPacket::player_input, payload});
    }(transport_guard_->get(), make_player_input(player_id_, dir.x, dir.y)));
}

void Client::send_recruit()
{
    assert(transport_guard_);
    co_spawn(
        ITransport::io(),
        [](std::shared_ptr<ITransport> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::recruit_soldier, payload});
        }(transport_guard_->get(), make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_interact()
{
    assert(transport_guard_);
    co_spawn(
        ITransport::io(),
        [](std::shared_ptr<ITransport> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::interact, payload});
        }(transport_guard_->get(), make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_rest()
{
    assert(transport_guard_);
    co_spawn(
        ITransport::io(),
        [](std::shared_ptr<ITransport> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::rest, payload});
        }(transport_guard_->get(), make_entity_id_payload(player_id_)),
        detached);
}

void Client::send_chat(std::string const &msg)
{
    assert(transport_guard_);
    chat_history_.push_back("You: " + msg);
    std::vector<uint8_t> p(msg.begin(), msg.end());
    ITransport::spawn(
        [](std::shared_ptr<ITransport> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::chat, std::move(payload)});
        }(transport_guard_->get(), std::move(p)));
}

void Client::send_dialogue_action(std::string const &action)
{
    assert(transport_guard_);
    ITransport::spawn(
        [](std::shared_ptr<ITransport> t, auto payload) -> awaitable<void> {
            co_await t->write({NetPacket::dialogue_action, std::move(payload)});
        }(transport_guard_->get(), std::vector<uint8_t>(action.begin(),
                                                        action.end())));
}

void Client::handle_message(ITransport &from, TransportMessage msg)
{
    // Quick handlers: tiny writes that don't touch remote_entities_
    if (msg.type == NetPacket::return_pid) {
        assert(msg.payload.size() >= 9);
        memcpy(&player_id_, msg.payload.data(), 8);
        player_team_ = static_cast<Team>(msg.payload[8]);
        spdlog::info("Client: Returned player ID from server: {} team: {}",
                     player_id_, player_team_);
        return;
    }
    if (msg.type == NetPacket::chat) {
        chat_history_.push_back(
            std::string(msg.payload.begin(), msg.payload.end()));
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
            std::erase_if(remote_entities_, [eid](RemoteEntity const &re) {
                return re.id == eid;
            });
        }
        break;
    case NetPacket::kicked: {
        std::string reason(msg.payload.begin(), msg.payload.end());
        spdlog::info("Client: kicked by server: {}", reason);
        detach_transport();
        app_->start_local_session();
        break;
    }
    case NetPacket::entity_update:
        handle_entity_update({msg.type, msg.payload});
        break;
    case NetPacket::combat_event: {
        auto ev = parse_combat_event(msg.payload);
        handle_combat_event(ev.attacker_id, ev.defender_id, ev.damage,
                            ev.killed);
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
    remote_entities_.push_back(std::move(re));
}

void Client::update(float dt)
{
    while (messages_.try_receive(
        [this](boost::system::error_code, std::shared_ptr<ITransport> t,
               TransportMessage msg) { handle_message(*t, std::move(msg)); })) {
    }

    if (player_id_ != invalid_entity) {
        interpolate_entities(dt);
    }
}

EntityId Client::local_player() const
{
    return player_id_;
}

Vec2f Client::player_position()
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
        break;
    case EntityKind::soldier:
        re.color = re.team == Team::enemy ? SDL_FColor{0.8f, 0.3f, 0.1f, 1.f}
                                          : SDL_FColor{0.3f, 0.5f, 0.9f, 1.f};
        re.scale = 0.8f;
        break;
    case EntityKind::npc:
        re.color = {0.8f, 0.6f, 0.2f, 1.f};
        re.scale = 1.f;
        break;
    case EntityKind::enemy:
        re.color = {0.9f, 0.2f, 0.1f, 1.f};
        re.scale = 1.f;
        break;
    case EntityKind::structure:
        re.color = {0.5f, 0.5f, 0.5f, 1.f};
        re.scale = 1.2f;
        break;
    default:
        re.color = {0.3f, 0.5f, 0.9f, 1.f};
        re.scale = 0.8f;
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
        Position pos;
        pos.read_sync(r);
        re.target_pos = pos.world_pos;
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
        re.facing = mov.facing;
    }
    if (mask & SyncComponent::soldier_ai) {
        SoldierAI ai;
        ai.read_sync(r);
    }
    if (mask & SyncComponent::interact)
        re.interactable = r.read<uint8_t>();
    if (mask & SyncComponent::survival) {
        pe.survival.read_sync(r);
        pe.has_survival = true;
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
    std::unordered_set<EntityId> seen;
    SyncReader r{data.data(), data.size()};

    parse_world_header(r, world_state_);

    while (!r.done()) {
        auto opt = parse_one_entity(r);
        if (!opt)
            break;
        auto &pe = *opt;
        auto &re = pe.re;
        seen.insert(re.id);

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
            if (pe.has_survival)
                player_survival_ = pe.survival;
            world_state_.reveal_radius(
                {static_cast<int>(re.target_pos.x / 64.f),
                 static_cast<int>(re.target_pos.y / 64.f)},
                8);
        }

        // Always upsert into remote_entities (including player for rendering)
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

    // Mark-and-sweep: remove entities not in this full sync
    std::erase_if(remote_entities_, [&](RemoteEntity const &re) {
        return !seen.contains(re.id);
    });
}

void Client::apply_sync_delta(std::vector<uint8_t> const &data)
{
    SyncReader r{data.data(), data.size()};

    parse_world_header(r, world_state_);

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
            if (pe.has_survival)
                player_survival_ = pe.survival;
            world_state_.reveal_radius(
                {static_cast<int>(re.target_pos.x / 64.f),
                 static_cast<int>(re.target_pos.y / 64.f)},
                8);
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

void Client::handle_combat_event(EntityId attacker_id, EntityId defender_id,
                                 int damage, bool killed)
{
    (void)attacker_id;
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
        throw std::runtime_error(
            "handle_dialogue_sync: data too short, maybe corrupted");
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
        dialogue_.history.push_back(
            {l.speaker == 0 ? DialogueLine::player : DialogueLine::npc,
             l.use_raw ? "" : l.text, l.use_raw ? l.text : "", l.use_raw,
             std::move(l.npc_name)});
    }
    dialogue_.available_topics = std::move(s.topics);
    dialogue_.available_actions = std::move(s.actions);
}

awaitable<bool> Client::authenticate_transport(std::shared_ptr<ITransport> t)
{
    try {
        spdlog::debug("Client: sending auth: payload: {}", auth_payload());
        co_await t->write({.type = NetPacket::auth, .payload = auth_payload()});
        auto res = co_await t->read();
        spdlog::debug("Client: auth result: type: {}, payload: {}", res.type,
                      res.payload);
        co_return res.type == NetPacket::auth &&res.payload == auth_payload();
    }
    catch (...) {
        co_return false;
    }
}
