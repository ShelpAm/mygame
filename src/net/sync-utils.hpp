#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "components/combat-stats.hpp"
#include "components/interactable.hpp"
#include "components/movement.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/survival-state.hpp"
#include "components/vision.hpp"
#include "net/sync-io.hpp"
#include "world/world-state.hpp"
#include <cstdint>
#include <flecs.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sync_util {

/// Context assembled from GameMode state that the sync helpers need.
struct SyncState {
    WorldState const &world_state;
    std::unordered_map<EntityId, std::unordered_set<Vec2i>> const &player_explored_tiles;
    std::unordered_map<EntityId, std::unordered_set<Vec2i>> const &player_visible_tiles;
    std::unordered_set<EntityId> const &dirty_entities;
    flecs::world const &ecs_world;
};

/// Payload for one full or delta sync frame.
struct SyncPayload {
    std::vector<uint8_t> bytes;
    std::unordered_set<EntityId> entity_ids;
};

// ── Serialisation helpers ────────────────────────────────────────────────────

/// Map a flecs entity to its EntityKind value.
uint8_t entity_kind(flecs::entity e);

// ── Component serialisation free functions ──────────────────────────────────
// These replace the write_sync/read_sync methods that were previously inline
// in each component header, decoupling ECS data from the network sync format.

void serialize_transform(SyncWriter &w, Transform const &t);
void deserialize_transform(SyncReader &r, Transform &t);

void serialize_combat_stats(SyncWriter &w, CombatStats const &cs);
void deserialize_combat_stats(SyncReader &r, CombatStats &cs);

void serialize_movement(SyncWriter &w, Movement const &m);
void deserialize_movement(SyncReader &r, Movement &m);

void serialize_soldier_ai(SyncWriter &w, SoldierAI const &ai);
void deserialize_soldier_ai(SyncReader &r, SoldierAI &ai);

void serialize_interactable(SyncWriter &w, Interactable const &i);
void deserialize_interactable(SyncReader &r, Interactable &i);

void serialize_survival_state(SyncWriter &w, SurvivalState const &s);
void deserialize_survival_state(SyncReader &r, SurvivalState &s);

void serialize_vision(SyncWriter &w, Vision const &v);
void deserialize_vision(SyncReader &r, Vision &v);

/// Binary-serialise one entity into the buffer.
void serialize_entity(flecs::entity e, std::vector<uint8_t> &out);

/// Write world-state header (day, season, time-of-day).
void write_world_header(std::vector<uint8_t> &out, WorldState const &ws);

/// Build a delta-sync payload: dirty + newly-visible entities.
SyncPayload build_dirty_payload(SyncState const &ss, EntityId player_eid,
                                std::unordered_set<EntityId> const &prev_sent);

/// Build a full-sync payload: all visible entities.
SyncPayload build_full_payload(SyncState const &ss, EntityId player_eid);

/// Check whether a world position is visible to a given player.
bool is_visible(SyncState const &ss, EntityId player_eid, Vec2f world_pos);

} // namespace sync_util
