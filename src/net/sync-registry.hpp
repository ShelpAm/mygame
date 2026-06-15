#pragma once

#include "entities/components/combat-stats.hpp"
#include "entities/components/interactable.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "net/net-packet.hpp"
#include "survival/condition-tracker.hpp"
#include <cstdint>

struct SyncEntry {
    uint16_t mask_bit;
    uint16_t wire_size;
};

inline constexpr SyncEntry kSyncTable[] = {
    {SyncComponent::entity_kind, 1},
    {SyncComponent::position, Position::kSyncWireSize},
    {SyncComponent::combat, CombatStats::kSyncWireSize},
    {SyncComponent::movement, Movement::kSyncWireSize},
    {SyncComponent::soldier_ai, SoldierAI::kSyncWireSize},
    {SyncComponent::interact, Interactable::kSyncWireSize},
    {SyncComponent::survival, SurvivalState::kSyncWireSize},
};

inline constexpr uint16_t wire_size(uint16_t mask)
{
    uint16_t sz = 0;
    for (auto const &entry : kSyncTable)
        if (mask & entry.mask_bit)
            sz += entry.wire_size;
    return sz;
}
