#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include <cstddef>
#include <vector>

enum class SoldierRole : uint8_t {
    melee = 0,
    ranged = 1,
    guard = 2,
};

// 姿态
enum class SoldierStance : uint8_t {
    // Only form the formation, won't attack
    passive,

    // Follow the formation, the if enemy in attacking range, attack them.
    defensive,

    // Hunt and destroy
    offensive,

    size_, // Don't use this as stance. It's just a way to automatically count
           // size
};

struct SoldierRoleDefaults {
    float max_speed;
    int max_hp;
    int hp;
    int attack;
    int defense;
    float attack_range;
    float engage_range;
    SoldierStance stance;
};

inline SoldierRoleDefaults soldier_role_stats(SoldierRole role)
{
    switch (role) {
    case SoldierRole::ranged:
        return {.max_speed = 200,
                .max_hp = 12,
                .hp = 12,
                .attack = 2,
                .defense = 1,
                .attack_range = 200.F,
                .engage_range = 200.F,
                .stance = SoldierStance::defensive};
    case SoldierRole::guard:
        return {.max_speed = 180,
                .max_hp = 20,
                .hp = 20,
                .attack = 4,
                .defense = 3,
                .attack_range = 48.F,
                .engage_range = 180.F,
                .stance = SoldierStance::offensive};
    case SoldierRole::melee:
    default:
        return {.max_speed = 200,
                .max_hp = 12,
                .hp = 12,
                .attack = 3,
                .defense = 2,
                .attack_range = 48.F,
                .engage_range = 200.F,
                .stance = SoldierStance::defensive};
    }
}

struct BelongsTo {};
struct Follows {};

struct SoldierAI {
    // EntityId leader;

    // follow_target: any moveable/inmoveable entity
    // EntityId follow_target; // [[deprecated]] use Follows
    float follow_distance = 8.F; // min distance to trigger move

    Vec2f formation_offset;
    float engage_range = 200.F;
    bool in_combat = false;
    SoldierRole role;
    SoldierStance stance = SoldierStance::defensive;

    // Pathfinding state (server-only, not synced)
    std::vector<Vec2i> path_;
    size_t path_index_ = 0;
    float path_recompute_timer_ = 0.F;
    Vec2i last_goal_tile_{};
    uint8_t path_stale_counter_ = 0;

    // follow_target(8) + formation_offset(8) + in_combat(1) + role(1) +
    // stance(1) = 19 bytes
    static constexpr uint16_t kSyncWireSize = 19;
};
