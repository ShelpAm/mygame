#include "net/sync-utils.hpp"
#include "components/building-data.hpp"
#include "components/combat-stats.hpp"
#include "components/defense-structure.hpp"
#include "components/entity-kind.hpp"
#include "components/interactable.hpp"
#include "components/movement.hpp"
#include "components/npc-state.hpp"
#include "components/player.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/survival-state.hpp"
#include "components/vision.hpp"
#include "net/net-packet.hpp"
#include "net/sync-io.hpp"
#include "world/map-data.hpp"
#include <cassert>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace sync_util {

// ── entity_kind ──────────────────────────────────────────────────────────────

uint8_t entity_kind(flecs::entity e)
{
    return static_cast<std::uint8_t>(to_kind_value(e.get<EntityKind>()));
}

// ── serialize_entity ─────────────────────────────────────────────────────────

void serialize_entity(flecs::entity e, std::vector<uint8_t> &out)
{
    auto const *ep = e.try_get<Transform>();
    assert(ep);

    uint16_t mask = SyncComponent::entity_kind | SyncComponent::position;
    // clang-format off
    if (e.has<CombatStats>())   mask |= SyncComponent::combat;
    if (e.has<Movement>())      mask |= SyncComponent::movement;
    if (e.has<SoldierAI>())     mask |= SyncComponent::soldier_ai;
    if (e.has<Interactable>())  mask |= SyncComponent::interact;
    if (e.has<SurvivalState>()) mask |= SyncComponent::survival;
    if (e.has<Vision>())        mask |= SyncComponent::vision;
    // clang-format on

    // --- 获取 config_name ---
    auto const *kt = e.try_get<EntityKind>();
    assert(kt && !kt->prototype.empty());
    std::string const &prototype = kt->prototype;

    // --- 打印实体摘要 ---
    spdlog::trace("Serializing entity {} (config='{}'), mask=0x{:04X}", e.id(), prototype, mask);

    SyncWriter w(out);
    w.write(e.id());
    w.write(mask);

    // entity kind config name
    w.write(static_cast<uint8_t>(prototype.size()));
    for (auto ch : prototype)
        w.write(static_cast<uint8_t>(ch));

    // For structures / buildings, also send subtype and footprint
    auto kind_value = to_kind_value(e.get<EntityKind>());
    if (kind_value == EntityKindValue::structure || kind_value == EntityKindValue::building) {
        auto const *bd = e.try_get<BuildingData>();
        assert(bd);
        uint8_t type = static_cast<uint8_t>(bd->type);
        uint8_t wdt = bd ? bd->width_tiles : 1;
        uint8_t hgt = bd ? bd->height_tiles : 1;
        spdlog::trace("  -> building: subtype={}, size={}x{}", type, wdt, hgt);
        w.write(type);
        w.write(wdt);
        w.write(hgt);
    }

    // position
    Transform const &t = e.get<Transform>();
    serialize_transform(w, t);
    spdlog::trace("  -> pos=({:.2f},{:.2f}), facing=({:.2f},{:.2f})", t.world_pos.x, t.world_pos.y,
                  t.facing.x, t.facing.y);

    // combat
    if (mask & SyncComponent::combat) {
        CombatStats const &cs = e.get<CombatStats>();
        serialize_combat_stats(w, cs);
        spdlog::trace("  -> hp={}/{}, alive={}, team={}, atk={}, def={}, range={}", cs.hp,
                      cs.max_hp, cs.alive, static_cast<int>(cs.team), cs.attack, cs.defense,
                      cs.attack_range);
    }

    // movement
    if (mask & SyncComponent::movement) {
        Movement const &m = e.get<Movement>();
        serialize_movement(w, m);
        spdlog::trace("  -> velocity=({:.2f},{:.2f})", m.velocity.x, m.velocity.y);
    }

    // soldier_ai
    if (mask & SyncComponent::soldier_ai) {
        SoldierAI const &ai = e.get<SoldierAI>();
        serialize_soldier_ai(w, ai);
        spdlog::trace("  -> role={}, stance={}, in_combat={}, offset=({:.2f},{:.2f})",
                      static_cast<int>(ai.role), static_cast<int>(ai.stance), ai.in_combat,
                      ai.formation_offset.x, ai.formation_offset.y);
    }

    // interact
    if (mask & SyncComponent::interact) {
        Interactable const &i = e.get<Interactable>();
        serialize_interactable(w, i);
        spdlog::trace("  -> interactable");
    }

    // survival
    if (mask & SyncComponent::survival) {
        SurvivalState const &s = e.get<SurvivalState>();
        serialize_survival_state(w, s);
        spdlog::trace("  -> food={:.1f}, water={:.1f}, health={:.1f}, energy={:.1f}", s.food,
                      s.water, s.health, s.energy);
    }

    // vision
    if (mask & SyncComponent::vision) {
        Vision const &v = e.get<Vision>();
        serialize_vision(w, v);
        spdlog::trace("  -> range={}, arc={}", v.range, v.arc);
    }
}

// ── write_world_header ───────────────────────────────────────────────────────

void write_world_header(std::vector<uint8_t> &out, WorldState const &ws)
{
    write_bytes(out, ws.day());
    write_bytes(out, static_cast<std::uint8_t>(ws.season()));
    write_float(out, ws.time_of_day());
}

// ── build_dirty_payload ──────────────────────────────────────────────────────

SyncPayload build_dirty_payload(SyncState const &ss, EntityId player_eid,
                                std::unordered_set<EntityId> const &prev_sent)
{
    SyncPayload result;
    write_world_header(result.bytes, ss.world_state);

    // Explored tiles
    {
        auto eit = ss.player_explored_tiles.find(player_eid);
        auto count =
            static_cast<uint16_t>(eit != ss.player_explored_tiles.end() ? eit->second.size() : 0);
        write_bytes(result.bytes, count);
        if (eit != ss.player_explored_tiles.end())
            for (auto const &t : eit->second) {
                write_bytes(result.bytes, static_cast<int32_t>(t.x));
                write_bytes(result.bytes, static_cast<int32_t>(t.y));
            }
    }

    auto it = ss.player_visible_tiles.find(player_eid);
    if (it == ss.player_visible_tiles.end())
        throw std::runtime_error("Player visible tiles not found for player_eid: " +
                                 std::to_string(player_eid));
    auto const &visible = it->second;

    auto emit = [&](flecs::entity e) {
        serialize_entity(e, result.bytes);
        result.entity_ids.insert(e.id());
    };

    // 1. Dirty entities on visible tiles
    for (auto eid : ss.dirty_entities) {
        auto e = ss.ecs_world.entity(eid);
        auto const *pos = e.try_get<Transform>();
        if (!pos)
            continue;
        emit(e);
    }

    // 2. Newly-visible entities
    ss.ecs_world.query<Transform>().each([&](flecs::entity e, Transform &pos) {
        if (result.entity_ids.contains(e.id()))
            return;
        if (e.id() == player_eid)
            return;
        if (prev_sent.contains(e.id()))
            return;
        if (visible.contains(world_to_tile(pos.world_pos)))
            emit(e);
    });

    return result;
}

// ── build_full_payload ───────────────────────────────────────────────────────

SyncPayload build_full_payload(SyncState const &ss, EntityId player_eid)
{
    SyncPayload result;
    write_world_header(result.bytes, ss.world_state);

    // Explored tiles
    {
        auto eit = ss.player_explored_tiles.find(player_eid);
        auto count =
            static_cast<uint16_t>(eit != ss.player_explored_tiles.end() ? eit->second.size() : 0);
        write_bytes(result.bytes, count);
        if (eit != ss.player_explored_tiles.end())
            for (auto const &t : eit->second) {
                write_bytes(result.bytes, static_cast<int32_t>(t.x));
                write_bytes(result.bytes, static_cast<int32_t>(t.y));
            }
    }

    auto it = ss.player_visible_tiles.find(player_eid);
    auto const &visible = it != ss.player_visible_tiles.end() ? it->second : decltype(it->second){};

    ss.ecs_world.query<EntityKind>().each([&](flecs::entity e, EntityKind const &kind) {
        spdlog::info("e.type={}", kind.prototype);
        bool should_send{};
        should_send |= e.id() == player_eid;
        should_send |=
            e.try_get<Transform>() && visible.contains(world_to_tile(e.get<Transform>().world_pos));
        should_send |= true; // TODO: Use entity.add<Replicated>() or so in the future.
        if (should_send) {
            serialize_entity(e, result.bytes);
            result.entity_ids.insert(e.id());
        }
    });
    return result;
}

// ── is_visible ───────────────────────────────────────────────────────────────

bool is_visible(SyncState const &ss, EntityId player_eid, Vec2f world_pos)
{
    auto it = ss.player_visible_tiles.find(player_eid);
    if (it == ss.player_visible_tiles.end())
        return false;
    return it->second.contains(world_to_tile(world_pos));
}

// ── Component serialisation free functions ───────────────────────────────────

void serialize_transform(SyncWriter &w, Transform const &t)
{
    w.write(t.world_pos);
    w.write(t.facing);
}
void deserialize_transform(SyncReader &r, Transform &t)
{
    t.world_pos = r.read<Vec2f>();
    t.facing = r.read<Vec2f>();
}

void serialize_combat_stats(SyncWriter &w, CombatStats const &cs)
{
    w.write(cs.hp);
    w.write(cs.max_hp);
    w.write(cs.alive);
    w.write(static_cast<uint8_t>(cs.team));
    w.write(cs.attack);
    w.write(cs.defense);
    w.write(cs.attack_range);
}
void deserialize_combat_stats(SyncReader &r, CombatStats &cs)
{
    cs.hp = r.read<int>();
    cs.max_hp = r.read<int>();
    cs.alive = r.read<bool>();
    cs.team = static_cast<Team>(r.read<uint8_t>());
    cs.attack = r.read<int>();
    cs.defense = r.read<int>();
    cs.attack_range = r.read<float>();
}

void serialize_movement(SyncWriter &w, Movement const &m)
{
    w.write(m.velocity);
}
void deserialize_movement(SyncReader &r, Movement &m)
{
    m.velocity = r.read<Vec2f>();
}

void serialize_soldier_ai(SyncWriter &w, SoldierAI const &ai)
{
    w.write(ai.formation_offset);
    w.write(ai.in_combat);
    w.write(static_cast<uint8_t>(ai.role));
    w.write(static_cast<uint8_t>(ai.stance));
}
void deserialize_soldier_ai(SyncReader &r, SoldierAI &ai)
{
    ai.formation_offset = r.read<Vec2f>();
    ai.in_combat = r.read<bool>();
    ai.role = static_cast<SoldierRole>(r.read<uint8_t>());
    ai.stance = static_cast<SoldierStance>(r.read<uint8_t>());
}

void serialize_interactable(SyncWriter &w, Interactable const & /*i*/)
{
    w.write(uint8_t{1});
}
void deserialize_interactable(SyncReader & /*r*/, Interactable & /*i*/)
{
    // Presence flag consumed externally
}

void serialize_survival_state(SyncWriter &w, SurvivalState const &s)
{
    w.write(s.food);
    w.write(s.water);
    w.write(s.health);
    w.write(s.energy);
}
void deserialize_survival_state(SyncReader &r, SurvivalState &s)
{
    s.food = r.read<float>();
    s.water = r.read<float>();
    s.health = r.read<float>();
    s.energy = r.read<float>();
}

void serialize_vision(SyncWriter &w, Vision const &v)
{
    w.write<int32_t>(v.range);
    w.write<float>(v.arc);
}
void deserialize_vision(SyncReader &r, Vision &v)
{
    v.range = r.read<int32_t>();
    v.arc = r.read<float>();
}

} // namespace sync_util
