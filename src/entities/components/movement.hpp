#pragma once

#include "core/math.hpp"
#include "net/sync-io.hpp"

struct Movement {
    Vec2f velocity;
    Vec2f target_pos;
    float speed = 200.f;
    bool moving = false;
    Vec2f facing;

    // velocity(8) + moving(1) + facing(8) = 17 bytes
    static constexpr uint16_t kSyncWireSize = 17;

    void write_sync(SyncWriter &w) const
    {
        w.write(velocity);
        w.write(moving);
        w.write(facing);
    }
    void read_sync(SyncReader &r)
    {
        velocity = r.read<Vec2f>();
        moving = r.read<bool>();
        facing = r.read<Vec2f>();
    }
};
