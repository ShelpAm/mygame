#include "systems/combat-system.hpp"
#include "components/collider.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <unordered_map>

bool CombatSystem::team_near_position(flecs::world &world, Team team, Vec2f pos, float radius) const
{
    bool found = false;
    world.query<CombatStats, Transform>().each([&](flecs::entity, CombatStats &cs, Transform &p) {
        if (!cs.alive || cs.team != team)
            return;
        Vec2f d = p.world_pos - pos;
        if (std::hypot(d.x, d.y) <= radius)
            found = true;
    });
    return found;
}
