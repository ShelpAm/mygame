#include "net/client.hpp"
#include "animation/animation-data.hpp"
#include "components/building-data.hpp"
#include "components/combat-stats.hpp"
#include "components/collider.hpp"
#include "components/entity-kind.hpp"
#include "components/interpolation-target.hpp"
#include "components/movement.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/survival-state.hpp"
#include "components/vision.hpp"
#include "components/visual/animation.hpp"
#include "components/visual/sprite.hpp"
#include "components/visual-fx.hpp"
#include "core/resource-manager.hpp"
#include "net/net-packet.hpp"
#include "net/sync-io.hpp"
#include "net/sync-utils.hpp"
#include "systems/formation.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <optional>
#include <rfl.hpp>
#include <rfl/yaml.hpp>
#include <spdlog/spdlog.h>
#include <unordered_map>

// ── helpers to initialise Sprite from sync data ─────────────────────────────

namespace {

struct SpriteDef {
    std::array<float, 2> origin;
    float scale;
    // Optional collider AABB (client-side debug rendering).
    bool has_collider = false;
    Vec2f collider_min{};
    Vec2f collider_max{};
};

// Loaded once from entities.yaml — maps EntityKind value → SpriteDef.
static std::unordered_map<uint8_t, SpriteDef> const &sprite_defs()
{
    static auto const cache = [] {
        std::unordered_map<uint8_t, SpriteDef> m;
        auto read_file = [](std::string const &p) {
            std::ifstream f(p);
            return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        };
        try {
            auto yaml = read_file("assets/data/entities.yaml");
            auto result = rfl::yaml::read<rfl::Generic>(yaml);
            if (!result) return m;

            using Obj = rfl::Generic::Object;
            auto &root = std::get<Obj>(result.value().get());
            for (auto const &[name, val] : root) {
                auto &obj = std::get<Obj>(val.get());
                auto vit = std::find_if(obj.begin(), obj.end(),
                    [](auto const &p) { return p.first == "visual"; });
                if (vit == obj.end()) continue;
                auto &vobj = std::get<Obj>(vit->second.get());
                auto oit = std::find_if(vobj.begin(), vobj.end(),
                    [](auto const &p) { return p.first == "origin"; });
                auto sit = std::find_if(vobj.begin(), vobj.end(),
                    [](auto const &p) { return p.first == "scale"; });
                if (oit == vobj.end() || sit == vobj.end()) continue;

                auto &arr = std::get<std::vector<rfl::Generic>>(oit->second.get());
                if (arr.size() < 2) continue;
                auto try_float = [](rfl::Generic const &g) -> float {
                    auto const &v = g.get();
                    if (auto *d = std::get_if<double>(&v)) return static_cast<float>(*d);
                    if (auto *i = std::get_if<int64_t>(&v)) return static_cast<float>(*i);
                    return 0.f;
                };
                float ox = try_float(arr[0]);
                float oy = try_float(arr[1]);
                float sc = try_float(sit->second);
                SpriteDef sd{{{ox, oy}}, sc};

                // Parse optional collider AABB from the entity config.
                auto cit = std::find_if(obj.begin(), obj.end(),
                    [](auto const &p) { return p.first == "collider"; });
                if (cit != obj.end()) {
                    if (auto *cobj = std::get_if<Obj>(&cit->second.get())) {
                        auto min_it = std::find_if(cobj->begin(), cobj->end(),
                            [](auto const &p) { return p.first == "min"; });
                        auto max_it = std::find_if(cobj->begin(), cobj->end(),
                            [](auto const &p) { return p.first == "max"; });
                        if (min_it != cobj->end() && max_it != cobj->end()) {
                            if (auto *min_a = std::get_if<std::vector<rfl::Generic>>(&min_it->second.get())) {
                                if (min_a->size() >= 2) {
                                    sd.has_collider = true;
                                    sd.collider_min.x = try_float((*min_a)[0]);
                                    sd.collider_min.y = try_float((*min_a)[1]);
                                }
                            }
                            if (auto *max_a = std::get_if<std::vector<rfl::Generic>>(&max_it->second.get())) {
                                if (max_a->size() >= 2) {
                                    sd.collider_max.x = try_float((*max_a)[0]);
                                    sd.collider_max.y = try_float((*max_a)[1]);
                                }
                            }
                        }
                    }
                }

                if (name == "player")                    m[EntityKind::player] = sd;
                else if (name.rfind("soldier_",0)==0)    m[EntityKind::soldier] = sd;
                else if (name == "npc_friendly")         m[EntityKind::npc] = sd;
                else if (name == "npc_hostile")          m[EntityKind::enemy] = sd;
                else if (name == "building")             m[EntityKind::structure] = sd;
            }
        } catch (std::exception const &e) {
            spdlog::error("sprite_defs: {}", e.what());
        }
        return m;
    }();
    return cache;
}

void init_sprite_from_kind(Sprite &s, uint8_t kind, uint8_t /*building_type*/,
                           Team team, SoldierRole role)
{
    auto const &defs = sprite_defs();
    auto it = defs.find(kind);
    if (it != defs.end()) {
        auto const &d = it->second;
        s.origin = {d.origin[0], d.origin[1]};
        s.scale = d.scale;
    }
}

} // anonymous namespace

/// Maps a server-side entity ID to the local ECS entity. Set on every entity
/// during parse_entity so the client can look up entities referenced by server
/// ID (entity_removed, combat_event, etc.).
struct ServerEntity {
    EntityId value;
};

// ── Client implementation ────────────────────────────────────────────────────

Client::Client() : messages_(Session::io(), 128)
{
    world_.component<Transform>();
    world_.component<CombatStats>();
    world_.component<Movement>();
    world_.component<SoldierAI>();
    world_.component<Vision>();
    world_.component<Interactable>();
    world_.component<SurvivalState>();
    world_.component<KindTag>();
    world_.component<InterpolationTarget>();
    world_.component<Animation>();
    world_.component<Sprite>();
    world_.component<ServerEntity>();

    interp_sys_ = world_.system<Transform, InterpolationTarget>("Interpolation")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Transform &t, InterpolationTarget &target) {
            if (auto *kt = e.try_get<KindTag>())
                if (kt->value == EntityKind::structure)
                    return; // structures don't move
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

    anim_ctrl_pipeline_ = world_.system<>("AnimationController")
        .kind(flecs::OnUpdate)
        .run([this](flecs::iter &it) {
            flecs::world w = it.world();
            anim_ctrl_sys_.update(w, *resources_, it.delta_time());
        });

    anim_sys_pipeline_ = world_.system<>("AnimationSystem")
        .kind(flecs::OnUpdate)
        .run([this](flecs::iter &it) {
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
    combat_events_.clear();
    chat_history_.clear();
    // Clear client ECS world
    world_.defer([this] { world_.each([](flecs::entity e) { e.destruct(); }); });
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
    static const SurvivalState empty;
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
    co_spawn(Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::recruit_soldier, payload});
        }(session_, make_entity_id_payload(server_player_id_)), detached);
}

void Client::send_recruit_ranged()
{
    assert(session_);
    co_spawn(Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::recruit_ranged, payload});
        }(session_, make_entity_id_payload(server_player_id_)), detached);
}

void Client::send_soldier_command()
{
    assert(session_);
    co_spawn(Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::soldier_command, payload});
        }(session_, make_entity_id_payload(server_player_id_)), detached);
}

void Client::send_respawn()
{
    assert(session_);
    co_spawn(Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::respawn, payload});
        }(session_, make_entity_id_payload(server_player_id_)), detached);
}

void Client::send_cycle_formation()
{
    assert(session_);
    int n = (int)formation_registry().size();
    formation_idx_ = (formation_idx_ + 1) % n;
    co_spawn(Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::formation, payload});
        }(session_, make_formation_payload(server_player_id_, selected_roles_)), detached);
}

void Client::send_interact()
{
    assert(session_);
    co_spawn(Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::interact, payload});
        }(session_, make_entity_id_payload(server_player_id_)), detached);
}

void Client::send_rest()
{
    assert(session_);
    co_spawn(Session::io(),
        [](std::shared_ptr<Session> t, auto payload) -> awaitable<void> {
            co_await t->write({ClientMsgType::rest, payload});
        }(session_, make_entity_id_payload(server_player_id_)), detached);
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
        memcpy(&player_id_, msg.payload.data(), 8);
        server_player_id_ = player_id_;
        spdlog::info("Client: Returned player ID from server: {}", player_id_);
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
            world_.query<ServerEntity>().each([&](flecs::entity e, ServerEntity const &se) {
                if (se.value == server_id)
                    e.destruct();
            });
        }
        break;
    case ServerMsgType::kicked: {
        std::string reason(msg.payload.begin(), msg.payload.end());
        spdlog::info("Client: kicked by server: {}", reason);
        session_->close();
        if (on_kicked_) on_kicked_();
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
    if (r.remaining() < 9) return;
    ws.set_day(r.read<int32_t>());
    ws.set_season(r.read<uint8_t>());
    ws.set_time_of_day(r.read<float>());
}

/// Create or update a client ECS entity from the wire reader.
/// Returns player-only extras (attack, defense, etc.) for the caller.
struct PlayerExtras {
    int attack = 0;
    int defense = 0;
    float attack_range = 80.f;
    SurvivalState survival;
    bool has_survival = false;
};

PlayerExtras parse_entity(SyncReader &r, flecs::world &w, ResourceManager const &resources)
{
    PlayerExtras pe{};

    EntityId id = r.read<uint64_t>();
    uint16_t mask = r.read<uint16_t>();

    // Create a fresh client entity and remember the server's entity ID on it.
    // We do NOT try to reuse the server's flecs entity_t value because the two
    // worlds have independent entity ID spaces and the same numeric ID may
    // refer to a dead generation on the client side.
    // Check if we already have a local entity for this server ID
    EntityId local_id = invalid_entity;
    w.query<ServerEntity>().each([&](flecs::entity ee, ServerEntity const &se) {
        if (se.value == id) local_id = ee.id();
    });
    auto e = local_id != invalid_entity
                 ? w.entity(local_id)
                 : w.entity().set(ServerEntity{id});

    // Track values across mask bits so we can initialise Animation below.
    uint8_t kind = 0;
    uint8_t building_type = 0;
    Team team = Team::neutral;
    SoldierRole role = SoldierRole::melee;

    if (mask & SyncComponent::entity_kind) {
        kind = r.read<uint8_t>();
        if (kind == EntityKind::structure) {
            building_type = r.read<uint8_t>();
            e.set(BuildingData{static_cast<BuildingData::Type>(building_type), "", ""});
        }
        e.set(KindTag{kind});
        e.set(CombatStats{}); // ensure entity exists
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
        team = cs.team;
        pe.attack = cs.attack;
        pe.defense = cs.defense;
        pe.attack_range = cs.attack_range;
    }

    if (mask & SyncComponent::movement) {
        Movement mov;
        deserialize_movement(r, mov);
        e.set(mov);
    }

    if (mask & SyncComponent::soldier_ai) {
        SoldierAI ai;
        deserialize_soldier_ai(r, ai);
        role = ai.role;
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

    // Initialise visual components for newly-created entities.
    // We wait until all sync data is available so the clip lookup has
    // kind, team and role.  The transitional bridge may later switch
    // clips based on movement/combat state.
    if (!e.has<Animation>()) {
        Sprite s;
        init_sprite_from_kind(s, kind, building_type, team, role);
        e.set(std::move(s));

        // Set collider from YAML for client-side debug rendering.
        {
            auto const &defs = sprite_defs();
            auto it = defs.find(kind);
            if (it != defs.end() && it->second.has_collider)
                e.set(Collider{it->second.collider_min, it->second.collider_max});
        }

        // Look up the default "idle" clip so AnimationSystem has
        // a valid clip from the very first frame.
        // Clip key prefix mirrors kind_key() in animation-data.cpp.
        char const *prefix = nullptr;
        switch (kind) {
        case EntityKind::player:    prefix = "player"; break;
        case EntityKind::soldier:
            prefix = (static_cast<int>(role) == 1) ? "archer"
                   : (team >= Team::enemy)          ? "goblin"
                                                    : "knight";
            break;
        case EntityKind::enemy:     prefix = "goblin"; break;
        case EntityKind::npc:       prefix = "villager"; break;
        case EntityKind::structure: prefix = "structure"; break;
        default:                    prefix = ""; break;
        }
        std::string key = std::string(prefix) + "_idle";
        Animation an;
        an.clip = resources.clip(key);
        // Last-resort fallback for any entity kind that has no clip registered
        if (!an.clip)
            an.clip = resources.clip("structure_idle");
        e.set(std::move(an));

        // Resolve the initial sprite name (first frame of the idle clip)
        // through sprites.yaml to populate texture_name and clipping rect.
        if (auto *cli = e.try_get<Animation>()) {
            if (cli->clip && !cli->clip->frame_textures.empty()) {
                auto const &sprite_name = cli->clip->frame_textures[0];
                auto *def = resources.resolve_sprite(sprite_name);
                if (def) {
                    Sprite updated;
                    auto const &defs = sprite_defs();
                    auto it = defs.find(kind);
                    if (it != defs.end()) {
                        updated.origin = {it->second.origin[0], it->second.origin[1]};
                        updated.scale = it->second.scale;
                    }
                    updated.texture_name = def->texture;
                    updated.offset = {def->clip[0], def->clip[1]};
                    updated.size   = {def->clip[2], def->clip[3]};
                    e.set(std::move(updated));
                }
            }
        }
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
        if (r.remaining() < 10) break;
        parse_entity(r, world_, *resources_);
    }

    // Resolve the player's server ID to the local entity ID.
    // (player_id_ was set from the return_pid message to the server's entity_t)
    EntityId server_pid = player_id_;
    world_.query<ServerEntity>().each([&](flecs::entity e, ServerEntity const &se) {
        if (se.value == server_pid) {
            player_id_ = e.id();
        }
    });
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
        if (r.remaining() < 10) break;
        parse_entity(r, world_, *resources_);
    }
}

void Client::handle_entity_update(std::vector<uint8_t> const &payload)
{
    if (payload.size() < 25) return;
    auto u = parse_entity_update(payload);

    // Find local entity by server ID
    EntityId local_id = invalid_entity;
    world_.query<ServerEntity>().each([&](flecs::entity e, ServerEntity const &se) {
        if (se.value == u.id)
            local_id = e.id();
    });
    if (local_id == invalid_entity)
        return;

    auto e = world_.entity(local_id);
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

    // Resolve server entity IDs to local IDs (non-player entities)
    auto to_local = [&](EntityId server_id) -> EntityId {
        if (server_id == player_id_) return server_id; // already local
        EntityId result = invalid_entity;
        world_.query<ServerEntity>().each([&](flecs::entity e, ServerEntity const &se) {
            if (se.value == server_id)
                result = e.id();
        });
        return result;
    };
    target = to_local(target);
    EntityId local_attacker = attacker_id != 0 ? to_local(attacker_id) : 0;

    if (target == invalid_entity) return;

    auto apply_hit = [&](EntityId eid) {
        auto e = world_.entity(eid);
        if (!e.is_alive()) return;
        auto &cs = e.get_mut<CombatStats>();
        cs.hp -= damage;
        if (killed) cs.alive = false;
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
                    player_visibility_.set_visible_arc(world_to_tile(t->world_pos), 6,
                                                       t->facing, 360.f);
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
        } else {
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
    } catch (...) {
        co_return false;
    }
}
