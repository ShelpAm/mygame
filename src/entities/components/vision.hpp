#pragma once

#include "net/sync-io.hpp"

struct Vision {
    int range = 6;
    float arc = 360.f;

    static constexpr uint16_t kSyncWireSize = 8;

    void write_sync(SyncWriter &w) const
    {
        w.write<int32_t>(range);
        w.write<float>(arc);
    }
    void read_sync(SyncReader &r)
    {
        range = r.read<int32_t>();
        arc = r.read<float>();
    }
};
