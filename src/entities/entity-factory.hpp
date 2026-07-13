#pragma once

#include "components/combat-stats.hpp"
#include "components/npc-state.hpp"
#include "components/soldier-ai.hpp"
#include "core/game-types.hpp"
#include "core/math.hpp"
#include "world/location-store.hpp"

#include <array>
#include <cstdint>
#include <flecs.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class MapData;
class RelationshipTable;
struct Formation;

// ── Entity template config (loaded from entities.yaml) ──────────────────────

/// Each entity type has its own sub-node in the YAML, freely structured.
/// No fixed structs — add any field without touching C++.

#include <rfl.hpp>
#include <rfl/yaml.hpp>
class EntityFactory {
  public:
    explicit EntityFactory(flecs::world &world);

    EntityFactory(EntityFactory const &) = delete;
    EntityFactory &operator=(EntityFactory const &) = delete;
    EntityFactory(EntityFactory &&) = delete;
    EntityFactory &operator=(EntityFactory &&) = delete;

    // ── Optional dependency injection ───────────────────────────────────────

    /// Called after every entity creation so the caller can mark it for sync.
    using DirtyCallback = std::function<void(EntityId)>;
    void set_dirty_callback(DirtyCallback cb) { dirty_cb_ = std::move(cb); }

    /// Needed by spawn_npc for initial relationship setup.
    void set_relationship_table(RelationshipTable *rt) { relationships_ = rt; }

    // ── Entity creation ─────────────────────────────────────────────────────

    EntityId spawn_player(Vec2f pos, Team team);

    /// Create a soldier entity. The `get_formation` callback is used to look up
    /// or create the Formation that controls this soldier's position offsets.
    EntityId
    spawn_soldier(EntityId captain_id, SoldierRole role, float elapsed,
                  std::function<Formation &(EntityId, SoldierRole, EntityId)> const &get_formation);

    EntityId spawn_npc(std::string const &id, std::string const &name, float x, float y,
                       std::string const &personality,
                       std::vector<NPCKnowledgeEntry> const &known_facts);

    /// Read building centroids from map tile groups and spawn one entity per
    /// building.
    void spawn_building_entities(std::vector<LocationDefinition> const &loc_defs,
                                 MapData const &map_data);

    /// Spawn town resident NPCs by querying existing building-entities in the
    /// flecs world.
    void spawn_town_npcs();

    /// Spawn one enemy soldier from the "enemy" entity config.
    EntityId spawn_enemy(Vec2f pos, Team team);

    void spawn(std::string const &kind, Vec2f pos);

  private:
    void mark_dirty(EntityId eid)
    {
        if (dirty_cb_)
            dirty_cb_(eid);
    }

    flecs::world &world_;
    DirtyCallback dirty_cb_;
    RelationshipTable *relationships_ = nullptr;
    rfl::Generic entities_cfg_;
};
