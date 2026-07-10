#pragma once

struct Vision {
    int range = 6;
    float arc = 360.f;

    static constexpr uint16_t kSyncWireSize = 8;
};
