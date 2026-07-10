#pragma once

#include "components/combat-stats.hpp"
#include "components/interactable.hpp"
#include "components/movement.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/vision.hpp"
#include "net/net-packet.hpp"
#include "components/survival-state.hpp"
#include <cstdint>
#include <vector>

struct SyncEntry {
    uint16_t mask_bit;
    uint16_t wire_size;

    constexpr SyncEntry(uint16_t bit, uint16_t size) : mask_bit(bit), wire_size(size) {}
};

inline std::vector<SyncEntry> const &kSyncTable()
{
    static std::vector<SyncEntry> table{
        {SyncComponent::entity_kind, 1},
        {SyncComponent::position, Position::kSyncWireSize},
        {SyncComponent::combat, CombatStats::kSyncWireSize},
        {SyncComponent::movement, Movement::kSyncWireSize},
        {SyncComponent::soldier_ai, SoldierAI::kSyncWireSize},
        {SyncComponent::interact, Interactable::kSyncWireSize},
        {SyncComponent::survival, SurvivalState::kSyncWireSize},
        {SyncComponent::vision, Vision::kSyncWireSize},
    };
    return table;
};

constexpr uint16_t wire_size(uint16_t mask)
{
    uint16_t sz = 0;
    for (auto const &entry : kSyncTable())
        if ((mask & entry.mask_bit) != 0)
            sz += entry.wire_size;
    return sz;
}
