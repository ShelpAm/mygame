#include "net/sync-utils.hpp"
#include "entities/components/building-data.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/defense-structure.hpp"
#include "entities/components/interactable.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/npc-state.hpp"
#include "entities/components/player.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/vision.hpp"
#include "net/net-packet.hpp"
#include "net/sync-io.hpp"
#include "survival/condition-tracker.hpp"
#include "world/map-data.hpp"
#include <cassert>
#include <stdexcept>

namespace sync_util {

// ── entity_kind ──────────────────────────────────────────────────────────────

uint8_t entity_kind(flecs::entity e)
{
    if (e.has<PlayerTag>())
        return EntityKind::player;
    if (e.has<NPCState>())
        return EntityKind::npc;
    if (e.has<SoldierAI>())
        return EntityKind::soldier;
    if (e.has<DefenseStructure>() || e.has<BuildingData>())
        return EntityKind::structure;
    return EntityKind::enemy;
}

// ── serialize_entity ─────────────────────────────────────────────────────────

void serialize_entity(flecs::entity e, std::vector<uint8_t> &out)
{
    auto const *ep = e.try_get<Transform>();
    auto const *ec = e.try_get<CombatStats>();
    assert(ep && ec);

    uint16_t mask = SyncComponent::entity_kind | SyncComponent::position | SyncComponent::combat;
    if (e.has<Movement>())
        mask |= SyncComponent::movement;
    if (e.has<SoldierAI>())
        mask |= SyncComponent::soldier_ai;
    if (e.has<Interactable>())
        mask |= SyncComponent::interact;
    if (e.has<SurvivalState>())
        mask |= SyncComponent::survival;
    if (e.has<Vision>())
        mask |= SyncComponent::vision;

    SyncWriter w(out);
    w.write(e.id());
    w.write(mask);

    // entity_kind (bit 0) — computed, not a component
    uint8_t kind = entity_kind(e);
    w.write(kind);

    // For structures, also send building type
    if (kind == EntityKind::structure) {
        auto const *bd = e.try_get<BuildingData>();
        w.write(static_cast<uint8_t>(bd ? static_cast<uint8_t>(bd->type) : 0));
    }

    // position (bit 1)
    e.get<Transform>().write_sync(w);

    // combat (bit 2)
    e.get<CombatStats>().write_sync(w);

    // movement (bit 3)
    if (mask & SyncComponent::movement)
        e.get<Movement>().write_sync(w);

    // soldier_ai (bit 4)
    if (mask & SyncComponent::soldier_ai)
        e.get<SoldierAI>().write_sync(w);

    // interact (bit 5) — presence flag
    if (mask & SyncComponent::interact)
        e.get<Interactable>().write_sync(w);

    // survival (bit 6)
    if (mask & SyncComponent::survival)
        e.get<SurvivalState>().write_sync(w);

    // vision (bit 7)
    if (mask & SyncComponent::vision)
        e.get<Vision>().write_sync(w);
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
        auto count = static_cast<uint16_t>(eit != ss.player_explored_tiles.end()
                                              ? eit->second.size()
                                              : 0);
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
    ss.ecs_world.query<Transform, CombatStats>().each(
        [&](flecs::entity e, Transform &pos, CombatStats &) {
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
        auto count = static_cast<uint16_t>(eit != ss.player_explored_tiles.end()
                                              ? eit->second.size()
                                              : 0);
        write_bytes(result.bytes, count);
        if (eit != ss.player_explored_tiles.end())
            for (auto const &t : eit->second) {
                write_bytes(result.bytes, static_cast<int32_t>(t.x));
                write_bytes(result.bytes, static_cast<int32_t>(t.y));
            }
    }

    auto it = ss.player_visible_tiles.find(player_eid);
    auto const &visible = it != ss.player_visible_tiles.end() ? it->second
                                                              : decltype(it->second){};

    ss.ecs_world.query<Transform, CombatStats>().each(
        [&](flecs::entity e, Transform &pos, CombatStats &) {
            if (e.id() == player_eid || visible.contains(world_to_tile(pos.world_pos))) {
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

} // namespace sync_util
