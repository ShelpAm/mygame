#pragma once

#include <cstdint>
#include <string>

/// Client-side tag storing the EntityKind value so rendering and animation
/// systems can filter by entity type.
struct EntityKind {
    std::string prototype;
};

enum class EntityKindValue : std::uint8_t {
    player,
    soldier_melee,
    soldier_ranged,
    soldier_guard,
    npc,
    enemy,
    structure,
    building,
    unofficial, // For moded conent or unknown entity types
};

inline EntityKindValue to_kind_value(EntityKind const &kind)
{
    if (kind.prototype == "player")
        return EntityKindValue::player;
    if (kind.prototype == "soldier_melee")
        return EntityKindValue::soldier_melee;
    if (kind.prototype == "soldier_ranged")
        return EntityKindValue::soldier_ranged;
    if (kind.prototype == "soldier_guard")
        return EntityKindValue::soldier_guard;
    if (kind.prototype == "npc" || kind.prototype == "npc_friendly" || kind.prototype == "npc_hostile")
        return EntityKindValue::npc;
    if (kind.prototype == "enemy")
        return EntityKindValue::enemy;
    if (kind.prototype == "structure")
        return EntityKindValue::structure;
    if (kind.prototype == "building" || kind.prototype == "inn" || kind.prototype == "market" ||
        kind.prototype == "temple" || kind.prototype == "blacksmith")
        return EntityKindValue::building;
    return EntityKindValue::unofficial;
}

inline EntityKind to_entity_kind(std::uint8_t value)
{
    switch (static_cast<EntityKindValue>(value)) {
    case EntityKindValue::player:
        return EntityKind{"player"};
    case EntityKindValue::soldier_melee:
        return EntityKind{"soldier_melee"};
    case EntityKindValue::soldier_ranged:
        return EntityKind{"soldier_ranged"};
    case EntityKindValue::soldier_guard:
        return EntityKind{"soldier_guard"};
    case EntityKindValue::npc:
        return EntityKind{"npc"};
    case EntityKindValue::enemy:
        return EntityKind{"enemy"};
    case EntityKindValue::structure:
        return EntityKind{"structure"};
    case EntityKindValue::building:
        return EntityKind{"building"};
    default:
        return EntityKind{"unofficial"};
    }
}
