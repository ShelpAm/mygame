#pragma once

#include "net/sync-io.hpp"

struct Interactable {
    float interact_radius = 64.f;
    bool can_talk = true;

    // Presence flag only (1 byte on wire — written externally)
    static constexpr uint16_t kSyncWireSize = 1;

    void write_sync(SyncWriter &w) const { w.write(uint8_t{1}); }
    void read_sync(SyncReader & /*r*/)
    {
        // Presence flag consumed externally
    }
};
