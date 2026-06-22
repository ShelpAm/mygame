#pragma once

#include "core/math.hpp"
#include "net/sync-io.hpp"

struct Movement {
    float max_speed;
    Vec2f velocity;

    // velocity(8) = 8 bytes
    static constexpr uint16_t kSyncWireSize = 8;

    void write_sync(SyncWriter &w) const { w.write(velocity); }
    void read_sync(SyncReader &r) { velocity = r.read<Vec2f>(); }
};
