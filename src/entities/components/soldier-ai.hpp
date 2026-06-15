#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "net/sync-io.hpp"

struct SoldierAI {
    EntityId follow_target = 0; // Entity to follow (player or captain)
    Vec2f formation_offset;     // Offset from leader in formation
    float follow_distance = 48.f;
    float engage_range = 200.f; // Distance to auto-engage enemies
    bool in_combat = false;

    // follow_target(8) + formation_offset(8) + in_combat(1) = 17 bytes
    static constexpr uint16_t kSyncWireSize = 17;

    void write_sync(SyncWriter &w) const
    {
        w.write(follow_target);
        w.write(formation_offset);
        w.write(in_combat);
    }
    void read_sync(SyncReader &r)
    {
        follow_target = r.read<EntityId>();
        formation_offset = r.read<Vec2f>();
        in_combat = r.read<bool>();
    }
};
