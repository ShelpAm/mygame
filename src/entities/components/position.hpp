#pragma once

#include "core/math.hpp"
#include "net/sync-io.hpp"

struct Position {
    Vec2f world_pos;

    static constexpr uint16_t kSyncWireSize = 8;

    void write_sync(SyncWriter &w) const { w.write(world_pos); }
    void read_sync(SyncReader &r) { world_pos = r.read<Vec2f>(); }
};
