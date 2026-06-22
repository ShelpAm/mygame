#pragma once

#include "core/math.hpp"
#include "net/sync-io.hpp"

struct Transform {
    Vec2f world_pos;
    Vec2f facing;

    static constexpr uint16_t kSyncWireSize = 16;

    void write_sync(SyncWriter &w) const
    {
        w.write(world_pos);
        w.write(facing);
    }
    void read_sync(SyncReader &r)
    {
        world_pos = r.read<Vec2f>();
        facing = r.read<Vec2f>();
    }
};
