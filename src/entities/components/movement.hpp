#pragma once

#include "core/math.hpp"
#include "net/sync-io.hpp"

struct Movement {
    Vec2f velocity;
    Vec2f target_pos;
    float speed = 200.f;
    Vec2f facing;

    // velocity(8) + facing(8) = 16 bytes
    static constexpr uint16_t kSyncWireSize = 16;

    void write_sync(SyncWriter &w) const
    {
        w.write(velocity);
        w.write(facing);
    }
    void read_sync(SyncReader &r)
    {
        velocity = r.read<Vec2f>();
        facing = r.read<Vec2f>();
    }
};
