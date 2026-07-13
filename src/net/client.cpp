#include "net/client.hpp"
#include "animation/animation-data.hpp"
#include "components/building-data.hpp"
#include "components/collider.hpp"
#include "components/combat-stats.hpp"
#include "components/entity-kind.hpp"
#include "components/interpolation-target.hpp"
#include "components/movement.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/survival-state.hpp"
#include "components/vision.hpp"
#include "components/visual-fx.hpp"
#include "components/visual/animation.hpp"
#include "components/visual/sprite.hpp"
#include "core/resource-manager.hpp"
#include "net/net-packet.hpp"
#include "net/sync-io.hpp"
#include "net/sync-utils.hpp"
#include "systems/formation.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>
#include <unordered_map>

/// Maps a server-side entity ID to the local ECS entity. Set on every entity
/// during parse_entity so the client can look up entities referenced by server
/// ID (entity_removed, combat_event, etc.).
struct ServerEntity {
    EntityId value;
};

// ── Client implementation ────────────────────────────────────────────────────

Client::Client() : anim_ctrl_sys_(world_), messages_(Session::io(), 128)
{
    world_.component<Transform>();
    world_.component<CombatStats>();
    world_.component<Movement>();
    world_.component<SoldierAI>();
    world_.component<Vision>();
    world_.component<Interactable>();
    world_.component<SurvivalState>();
    world_.component<EntityKind>();
    world_.component<InterpolationTarget>();
    world_.component<Animation>();
    world_.component<Sprite>();
    world_.component<ServerEntity>();

    interp_sys_ = world_.system<Transform, InterpolationTarget>("Interpolation")
                      .kind(flecs::OnUpdate)
                      .each([](flecs::entity e, Transform &t, InterpolationTarget &target) {
                          float rate = std::min(1.f, e.world().delta_time() * 30.f);
                          t.world_pos += (target.position - t.world_pos) * rate;
                          t.facing += (target.facing - t.facing) * rate;
                      });

    // ── Animation pipeline (injected from outside, no flecs coupling in the systems) ──
    //
    // Both AnimationControllerSystem and AnimationSystem stay clean — they
    // accept (world, dt) via a plain update() call.  The pipeline registration
    // lives here in Client, wiring them into world_.progress() via lambda
    // delegates so the ordering is explicit at the application level.

    anim_ctrl_pipeline_ =
        world_.system<>("AnimationController").kind(flecs::OnUpdate).run([this](flecs::iter &it) {
            flecs::world w = it.world();
            anim_ctrl_sys_.update(w, *resources_, it.delta_time());
        });

    anim_sys_pipeline_ =
        world_.system<>("AnimationSystem").kind(flecs::OnUpdate).run([this](flecs::iter &it) {
            flecs::world w = it.world();
            anim_sys_.update(w, *resources_, it.delta_time());
        });

    // Ordering: interpolation → clip-switching → frame advancement
    anim_ctrl_pipeline_.depends_on(interp_sys_);
    anim_sys_pipeline_.depends_on(anim_ctrl_pipeline_);

    // Floating text update: drift upward, fade alpha, auto-destruct on expiry.
    world_.system<FloatingText, Transform>("FloatingTextUpdate")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, FloatingText &ft, Transform &t) {
            float dt = e.world().delta_time();
            ft.elapsed += dt;
            t.world_pos += ft.velocity * dt;
            if (ft.elapsed >= ft.lifetime)
                e.destruct();
        });

    spdlog::info("Client: initialized");
}

Client::~Client()
{
    spdlog::info("Client: destructing this");
}

// ── Transport ────────────────────────────────────────────────────────────────

awaitable<void> Client::attach_transport(std::shared_ptr<Session> t)
{
    spdlog::debug("Client: attaching transport {}", t->remote_info());

    if (!co_await authenticate_transport(t))
        throw std::runtime_error("Server verification failed: unexpected response");

    spdlog::info("Client: auth successful ({})", t->remote_info());

    session_ = t;
    spdlog::info("Client: transport ({}) attached", t->remote_info());

    auto reading_loop = [](Client *c, std::shared_ptr<Session> t) -> awaitable<void> {
        try {
            while (true) {
                auto msg = co_await t->read();
                spdlog::log((msg.type == static_cast<std::uint32_t>(ServerMsgType::state_delta)
                                 ? spdlog::level::trace
                                 : spdlog::level::debug),
                            "Client: received message {} with payload size {} "
                            "from transport {}",
                            static_cast<ServerMsgType>(msg.type), msg.payload.size(),
                            t->remote_info());
                if (!c->messages_.try_send(boost::system::error_code{}, t, msg))
                    Session::spawn([](Client *c, auto t, auto msg) -> awaitable<void> {
                        co_await c->messages_.async_send(boost::system::error_code{}, t,
                                                         std::move(msg));
                    }(c, t, std::move(msg)));
            }
        }
        catch (boost::system::system_error const &e) {
            c->detach_transport(detach_token{}, t.get());
            if (e.code() == asio::error::operation_aborted || e.code() == asio::error::eof ||
                e.code() == asio::experimental::error::channel_closed ||
                e.code() == asio::experimental::error::channel_cancelled) {
                spdlog::debug("Client: transport {} closed because {}", t->remote_info(), e.what());
                co_return;
            }
            throw;
        }
    };
    Session::spawn(reading_loop(this, t));
}

void Client::detach_transport(detach_token, Session *s)
{
    spdlog::info("Client: transport {} detached", s->remote_info());

    player_id_ = invalid_entity;
    server_player_id_ = invalid_entity;
    server_to_local_.clear();
    combat_events_.clear();
    chat_history_.clear();
    // Clear client ECS world — only user entities (those with Transform).
    // Delete one at a time without defer to avoid batched reparenting crash.
    std::vector<flecs::entity_t> to_delete;
    world_.each<Transform>([&to_delete](flecs::entity e, Transform &) { to_delete.push_back(e); });
    for (auto id : to_delete)
        world_.entity(id).destruct();
}

// ── Accessors reading from ECS player entity ────────────────────────────────

Vec2f Client::player_position() const
{
    if (player_id_ == invalid_entity)
        throw std::runtime_error("player_position: player ID not set");
    auto e = world_.entity(player_id_);
    auto *t = e.is_alive() ? e.try_get<Transform>() : nullptr;
    if (!t)
        throw std::runtime_error("player_position: player entity not alive");
    return t->world_pos;
}

Team Client::player_team() const
{
    auto e = world_.entity(player_id_);
    auto *cs = e.is_alive() ? e.try_get<CombatStats>() : nullptr;
    return cs ? cs->team : Team::neutral;
}

bool Client::is_player_dead()
{
    if (player_id_ == invalid_entity)
        throw std::runtime_error("is_player_dead: player ID not set");
    auto e = world_.entity(player_id_);
    auto *cs = e.is_alive() ? e.try_get<CombatStats>() : nullptr;
    return !cs || !cs->alive;
}

CombatStats const *Client::player_stats()
{
    if (player_id_ == invalid_entity)
        return nullptr;
    auto e = world_.entity(player_id_);
    return e.is_alive() ? e.try_get<CombatStats>() : nullptr;
}

SurvivalState const &Client::survival() const
{
    static SurvivalState const empty;
    auto e = world_.entity(player_id_);
    auto *s = e.is_alive() ? e.try_get<SurvivalState>() : nullptr;
    return s ? *s : empty;
}

// ── Input helpers ────────────────────────────────────────────────────────────

void Client::send_join_request()
{
    if (!session_ || !session_->is_open())
        throw std::runtime_error("send_join_request: transport not attached or closed");
    Session::spawn([](std::shared_ptr<Session> t) -> awaitable<void> {
        co_await t->write({ClientMsgType::join, std::vector<std::uint8_t>{}});
    }(session_));
}

void Client::send_player_direction(Vec2f dir)
{
    if (!session_ || !session_->is_open()) {
        throw std::runtime_error("send_player_direction: transport not attached or closed");
    }
    spdlog::trace("Client: sending move dir: {}, {} (tick={})", dir.x, dir.y, SDL_GetTicks());
    last_active_send_tick_ = std::chrono::steady_clock::now();
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        co_await t->write({ClientMsgType::player_input, payload});
    }(session_, make_player_input(server_player_id_, dir.x, dir.y, SDL_GetTicks())));
}

void Client::send_recruit()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::recruit_soldier, payload});
        }(session_, make_entity_id_payload(server_player_id_)),
        detached);
}

void Client::send_recruit_ranged()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::recruit_ranged, payload});
        }(session_, make_entity_id_payload(server_player_id_)),
        detached);
}

void Client::send_soldier_command()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::soldier_command, payload});
        }(session_, make_entity_id_payload(server_player_id_)),
        detached);
}

void Client::send_respawn()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::respawn, payload});
        }(session_, make_entity_id_payload(server_player_id_)),
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
            co_await t->write({ClientMsgType::formation, payload});
        }(session_, make_formation_payload(server_player_id_, selected_roles_)),
        detached);
}

void Client::send_interact()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::interact, payload});
        }(session_, make_entity_id_payload(server_player_id_)),
        detached);
}

void Client::send_rest()
{
    assert(session_);
    co_spawn(
        Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::rest, payload});
        }(session_, make_entity_id_payload(server_player_id_)),
        detached);
}

void Client::send_chat(std::string const &msg)
{
    assert(session_);
    chat_history_.push_back("You: " + msg);
    std::vector<uint8_t> p(msg.begin(), msg.end());
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        co_await t->write({ClientMsgType::chat, std::move(payload)});
    }(session_, std::move(p)));
}

void Client::send_dialogue_action(std::string const &action)
{
    assert(session_);
    Session::spawn([](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
        co_await t->write({ClientMsgType::dialogue_action, std::move(payload)});
    }(session_, std::vector<uint8_t>(action.begin(), action.end())));
}

// ── Message handling ─────────────────────────────────────────────────────────

void Client::handle_message([[maybe_unused]] Session &from, TransportMessage msg)
{
    auto st = static_cast<ServerMsgType>(msg.type);

    if (st == ServerMsgType::return_pid) {
        assert(msg.payload.size() >= 9);
        memcpy(&server_player_id_, msg.payload.data(), 8);
        spdlog::info("Client: Returned player ID from server: {}", server_player_id_);
        return;
    }
    if (st == ServerMsgType::chat) {
        chat_history_.push_back(std::string(msg.payload.begin(), msg.payload.end()));
        return;
    }
    if (st == ServerMsgType::dialogue_sync) {
        handle_dialogue_sync(msg.payload);
        return;
    }

    switch (st) {
    case ServerMsgType::state_full:
        apply_sync_full(msg.payload);
        record_sync_received();
        break;
    case ServerMsgType::state_delta:
        apply_sync_delta(msg.payload);
        record_sync_received();
        break;
    case ServerMsgType::entity_removed:
        if (msg.payload.size() >= 8) {
            EntityId server_id{};
            memcpy(&server_id, msg.payload.data(), 8);
            if (auto it = server_to_local_.find(server_id); it != server_to_local_.end()) {
                world_.entity(it->second).destruct();
                server_to_local_.erase(it);
            }
        }
        break;
    case ServerMsgType::kicked: {
        std::string reason(msg.payload.begin(), msg.payload.end());
        spdlog::info("Client: kicked by server: {}", reason);
        session_->close();
        if (on_kicked_)
            on_kicked_();
        break;
    }
    case ServerMsgType::entity_update:
        handle_entity_update(msg.payload);
        break;
    case ServerMsgType::combat_event: {
        auto ev = parse_combat_event(msg.payload);
        handle_combat_event(ev.attacker_id, ev.defender_id, ev.damage, ev.killed);
        break;
    }
    case ServerMsgType::projectile_fired: {
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
    case ServerMsgType::town_discovered: {
        auto td = parse_town_discovered(msg.payload);
        auto it = std::ranges::find_if(discovered_towns_,
                                       [&](auto const &t) { return t.loc_id == td.loc_id; });
        if (it == discovered_towns_.end())
            discovered_towns_.push_back({td.loc_id, td.locale_key, false});
        current_town_id_ = td.loc_id;
        break;
    }
    case ServerMsgType::town_left:
        current_town_id_.clear();
        break;
    default:
        break;
    }
}

// ── ECS sync helpers ─────────────────────────────────────────────────────────

namespace {
using namespace sync_util;

void parse_world_header(SyncReader &r, WorldState &ws)
{
    if (r.remaining() < 9)
        return;
    ws.set_day(r.read<int32_t>());
    ws.set_season(r.read<uint8_t>());
    ws.set_time_of_day(r.read<float>());
}

/// Create or update a client ECS entity from the wire reader.
struct PlayerExtras {
    SurvivalState survival;
    bool has_survival = false;
};

PlayerExtras parse_entity(SyncReader &r, flecs::world &w, ResourceManager const &resources,
                          std::unordered_map<EntityId, flecs::entity_t> &s2l)
{
    PlayerExtras pe{};

    EntityId id = r.read<uint64_t>();
    uint16_t mask = r.read<uint16_t>();

    // Look up existing local entity, or create a new one.
    // Map enables O(1) lookup instead of an ECS query scan.
    EntityId local_id = invalid_entity;
    if (auto it = s2l.find(id); it != s2l.end())
        local_id = it->second;
    auto e = local_id != invalid_entity ? w.entity(local_id) : w.entity().set(ServerEntity{id});
    s2l[id] = e;

    // Track values across mask bits so we can initialise Animation below.
    EntityKindValue kind_value{};
    uint8_t building_type = 0;
    std::string entity_cfg_name;

    if (mask & SyncComponent::entity_kind) {
        // Read entity config name (entities.yaml key, e.g. "soldier_melee")
        uint8_t name_len = r.read<uint8_t>();
        entity_cfg_name.assign(name_len, '\0');
        for (uint8_t i = 0; i < name_len; ++i)
            entity_cfg_name[static_cast<size_t>(i)] = static_cast<char>(r.read<uint8_t>());
        kind_value = to_kind_value({.prototype = entity_cfg_name});

        if (kind_value == EntityKindValue::structure || kind_value == EntityKindValue::building) {
            building_type = r.read<uint8_t>();
            uint8_t bw = r.read<uint8_t>();
            uint8_t bh = r.read<uint8_t>();
            e.set(BuildingData{static_cast<BuildingData::Type>(building_type), "", "", 0, bw, bh});
        }
        {
            e.set(EntityKind{entity_cfg_name});

            // ── 视觉组件初始化 ────────────────────────────────────────
            // 每个同步实体都需要 Sprite + Animation，这样渲染/动画系统
            // 的 ECS 查询直接匹配，无需额外的解引用/分支。
            if (!e.has<Animation>()) {
                Sprite s;
                std::string clip_group;

                try {
                    auto const &cfg = resources.entity_config(entity_cfg_name);

                    // 1. clip 组名 — 直接从 entity_cfg_name 对应的配置读取，
                    // 每种建筑类型（inn / market / temple / blacksmith）各有
                    // 独立的 entities.yaml 条目，不再需要 subtypes 映射。
                    clip_group = cfg.clip.value_or("");

                    // 2. 视觉默认值
                    if (cfg.visual) {
                        s.origin = {cfg.visual->origin[0], cfg.visual->origin[1]};
                        s.scale = cfg.visual->scale;
                    }

                    // 3. 碰撞体
                    if (cfg.collider)
                        e.set(Collider{{cfg.collider->min[0], cfg.collider->min[1]},
                                       {cfg.collider->max[0], cfg.collider->max[1]}});
                }
                catch (std::runtime_error const &) {
                }

                // 4. Animation: idle clip
                auto const *idle_clip = resources.clip(clip_group + "_idle");
                std::string fallback{"unavailable_idle"};
                assert(resources.clip(fallback));
                if (!idle_clip) {
                    spdlog::warn(
                        "parse_entity: clip group '{}' has no idle clip, using fallback '{}'",
                        clip_group, fallback);
                    idle_clip = resources.clip(fallback);
                }

                assert(idle_clip);
                // 5. Sprite 纹理 / 裁剪矩形 ← 从 idle clip 的第一帧推导
                if (!idle_clip->frame_sprites.empty()) {
                    s.name = idle_clip->frame_sprites[0];
                    if (auto *sdef = resources.resolve_sprite(idle_clip->frame_sprites[0])) {
                        s.texture_name = sdef->texture;
                        s.offset = {sdef->clip[0], sdef->clip[1]};
                        s.size = {sdef->clip[2], sdef->clip[3]};
                    }
                }
                // 6. 建筑: 按 footprint 放大
                if (kind_value == EntityKindValue::building) {
                    auto const *bd = e.try_get<BuildingData>();
                    float f = static_cast<float>(
                        std::max(bd ? bd->width_tiles : 1, bd ? bd->height_tiles : 1));
                    s.scale *= f;
                }
                e.set(s);
                e.set(Animation{idle_clip});
            }
        }
    }

    if (mask & SyncComponent::position) {
        Transform pos;
        deserialize_transform(r, pos);
        e.set(Transform{pos.world_pos, pos.facing});
        e.set(InterpolationTarget{pos.world_pos, pos.facing});
    }

    if (mask & SyncComponent::combat) {
        CombatStats cs;
        deserialize_combat_stats(r, cs);
        e.set(cs);
    }

    if (mask & SyncComponent::movement) {
        Movement mov;
        deserialize_movement(r, mov);
        e.set(mov);
    }

    if (mask & SyncComponent::soldier_ai) {
        SoldierAI ai;
        deserialize_soldier_ai(r, ai);
        ai.path_.clear();
        ai.path_index_ = 0;
        e.set(ai);
    }

    if (mask & SyncComponent::interact) {
        if (r.read<uint8_t>())
            e.set(Interactable{});
        else
            e.remove<Interactable>();
    }

    if (mask & SyncComponent::survival) {
        deserialize_survival_state(r, pe.survival);
        pe.has_survival = true;
        e.set(pe.survival);
    }

    if (mask & SyncComponent::vision) {
        Vision v;
        deserialize_vision(r, v);
        e.set(v);
    }

    return pe;
}

} // anonymous namespace

void Client::apply_sync_full(std::vector<uint8_t> const &data)
{
    SyncReader r{data.data(), data.size()};
    parse_world_header(r, world_state_);

    {
        auto count = r.read<uint16_t>();
        for (uint16_t i = 0; i < count; ++i)
            player_visibility_.explore_single(Vec2i(r.read<int32_t>(), r.read<int32_t>()));
    }

    while (!r.done()) {
        if (r.remaining() < 10)
            break;
        parse_entity(r, world_, *resources_, server_to_local_);
    }

    // Resolve the local player entity from the server→local map.
    if (auto it = server_to_local_.find(server_player_id_); it != server_to_local_.end())
        player_id_ = it->second;
}

void Client::apply_sync_delta(std::vector<uint8_t> const &data)
{
    SyncReader r{data.data(), data.size()};
    parse_world_header(r, world_state_);

    {
        auto count = r.read<uint16_t>();
        for (uint16_t i = 0; i < count; ++i)
            player_visibility_.explore_single(Vec2i(r.read<int32_t>(), r.read<int32_t>()));
    }

    while (!r.done()) {
        if (r.remaining() < 10)
            break;
        parse_entity(r, world_, *resources_, server_to_local_);
    }
}

void Client::handle_entity_update(std::vector<uint8_t> const &payload)
{
    if (payload.size() < 25)
        return;
    auto u = parse_entity_update(payload);

    // Find local entity by server ID
    auto it = server_to_local_.find(u.id);
    if (it == server_to_local_.end())
        return;

    auto e = world_.entity(it->second);
    e.set(Transform{Vec2f{u.x, u.y}});
    CombatStats cs;
    cs.hp = u.hp;
    cs.max_hp = u.max_hp;
    cs.alive = u.alive;
    e.set(cs);
    if (!e.has<Animation>()) {
        e.set(Sprite{});
        e.set(Animation{});
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

    // Resolve server entity IDs to local IDs
    auto to_local = [&](EntityId server_id) -> EntityId {
        auto it = server_to_local_.find(server_id);
        return it != server_to_local_.end() ? it->second : invalid_entity;
    };
    target = to_local(target);
    EntityId local_attacker = attacker_id != 0 ? to_local(attacker_id) : 0;

    if (target == invalid_entity)
        return;

    auto apply_hit = [&](EntityId eid) {
        auto e = world_.entity(eid);
        if (!e.is_alive())
            return;
        auto &cs = e.get_mut<CombatStats>();
        cs.hp -= damage;
        if (killed)
            cs.alive = false;
        e.set(HitFlash{0.15f, 0.3f});
    };

    apply_hit(target);

    if (local_attacker != 0) {
        auto ae = world_.entity(local_attacker);
        if (ae.is_alive())
            ae.set(HitFlash{0.15f, 0.3f});
    }

    combat_events_.push_back({attacker_id, defender_id, damage, killed});
}

// ── Per-frame update ─────────────────────────────────────────────────────────

void Client::update(float dt)
{
    while (messages_.try_receive(
        [this](boost::system::error_code, std::shared_ptr<Session> const &t, TransportMessage msg) {
            handle_message(*t, std::move(msg));
        })) {
    }

    if (connected()) {
        // Run client ECS systems (interpolation → controller → animation via pipeline)
        world_.progress(dt);

        // Update player visibility from ECS player entity
        auto pe = world_.entity(player_id_);
        if (pe.is_alive()) {
            auto *t = pe.try_get<Transform>();
            auto *v = pe.try_get<Vision>();
            if (t) {
                if (v)
                    player_visibility_.set_visible_arc(world_to_tile(t->world_pos), v->range,
                                                       t->facing, v->arc);
                else
                    player_visibility_.set_visible_arc(world_to_tile(t->world_pos), 6, t->facing,
                                                       360.f);
            }
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

// ── Diagnostics ──────────────────────────────────────────────────────────────

void Client::record_sync_received()
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    last_sync_recv_tick_ = now;
    if (last_active_send_tick_.time_since_epoch().count() > 0) {
        auto rtt = static_cast<uint32_t>(
            duration_cast<milliseconds>(now - last_active_send_tick_).count());
        estimated_rtt_ms_ =
            estimated_rtt_ms_ == 0 ? rtt : estimated_rtt_ms_ * 7 / 10 + rtt * 3 / 10;
        last_active_send_tick_ = {};
    }
}

// ── Dialogue sync ────────────────────────────────────────────────────────────

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
        DialogueLine dl;
        dl.speaker = l.speaker == 0 ? DialogueLine::player : DialogueLine::npc;
        dl.npc_name = std::move(l.npc_name);
        if (l.use_template) {
            dl.use_template = true;
            dl.template_type = std::move(l.template_type);
            dl.variant_index = l.variant_index;
            for (auto &kv : l.slots)
                dl.slots.emplace(std::move(kv.first), std::move(kv.second));
        }
        else {
            dl.text_key = l.use_raw ? "" : l.text;
            dl.raw_text = l.use_raw ? l.text : "";
            dl.use_raw = l.use_raw;
        }
        dialogue_.history.push_back(std::move(dl));
    }
    dialogue_.available_topics = std::move(s.topics);
    dialogue_.available_actions = std::move(s.actions);
}

// ── Auth ─────────────────────────────────────────────────────────────────────

awaitable<bool> Client::authenticate_transport(std::shared_ptr<Session> t)
{
    try {
        spdlog::debug("Client: sending auth: payload: {}", auth_payload());
        co_await t->write({ClientMsgType::auth, auth_payload()});
        auto res = co_await t->read();
        spdlog::debug("Client: auth result: type: {}, payload: {}",
                      static_cast<ServerMsgType>(res.type), res.payload);
        co_return res.type == static_cast<std::uint32_t>(ServerMsgType::auth) &&
            res.payload == auth_payload();
    }
    catch (...) {
        co_return false;
    }
}
