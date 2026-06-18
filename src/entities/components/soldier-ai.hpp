#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "net/sync-io.hpp"

enum class SoldierRole : uint8_t {
    melee = 0,
    ranged = 1,
};

// 0=follow, 1=guard (stand ground), 2=patrol (follow + guard)
enum class SoldierStance : uint8_t {
    follow = 0,
    guard = 1,
    patrol = 2,
};

struct SoldierAI {
    EntityId follow_target = 0;
    Vec2f formation_offset;
    float follow_distance = 48.F;
    float engage_range = 200.F;
    bool in_combat = false;
    SoldierRole role = SoldierRole::melee;
    SoldierStance stance = SoldierStance::follow;
    Vec2f guard_post; // return-to position for guard stance (server-only, not
                      // synced)

    // follow_target(8) + formation_offset(8) + in_combat(1) + role(1) +
    // stance(1) = 19 bytes
    static constexpr uint16_t kSyncWireSize = 19;

    void write_sync(SyncWriter &w) const
    {
        w.write(follow_target);
        w.write(formation_offset);
        w.write(in_combat);
        w.write(static_cast<uint8_t>(role));
        w.write(static_cast<uint8_t>(stance));
    }
    void read_sync(SyncReader &r)
    {
        follow_target = r.read<EntityId>();
        formation_offset = r.read<Vec2f>();
        in_combat = r.read<bool>();
        role = static_cast<SoldierRole>(r.read<uint8_t>());
        stance = static_cast<SoldierStance>(r.read<uint8_t>());
    }
};
