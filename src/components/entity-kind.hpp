#pragma once

#include <cstdint>

/// Client-side tag storing the EntityKind value so rendering and animation
/// systems can filter by entity type.
struct KindTag {
    uint8_t value = 0;
};
