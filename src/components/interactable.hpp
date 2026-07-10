#pragma once

#include <cstdint>

struct Interactable {
    float interact_radius{};
    bool can_talk = true;

    // Presence flag only (1 byte on wire — written externally)
    static constexpr uint16_t kSyncWireSize = 1;
};
