#include "systems/combat-system.hpp"
#include "entities/components/collider.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/sprite.hpp"
#include "systems/combat-utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

bool CombatSystem::team_near_position(flecs::world &world, Team team, Vec2f pos,
                                      float radius) const
{
    bool found = false;
    world.query<CombatStats, Position>().each(
        [&](flecs::entity, CombatStats &cs, Position &p) {
            if (!cs.alive || cs.team != team)
                return;
            Vec2f d = p.world_pos - pos;
            if (std::sqrt(d.x * d.x + d.y * d.y) <= radius)
                found = true;
        });
    return found;
}

void CombatSystem::spawn_enemy_wave(flecs::world &world, int count,
                                    Vec2f center, float spread, Team team,
                                    std::vector<EntityId> *out_ids)
{
    for (int i = 0; i < count; ++i) {
        auto e = world.entity();
        float ang = (float)(std::rand() % 360) * 3.14159f / 180.f;
        float dist = (float)(std::rand() % (int)spread);
        float x = center.x + std::cos(ang) * dist;
        float y = center.y + std::sin(ang) * dist;

        e.set<Position>(
            Position{{x, y}, {(int)(x / 64.f), (int)(y / 64.f)}, 0.5f});
        e.set<Sprite>(Sprite{.origin = {12.f, 12.f},
                             .color = {0.9f, 0.2f, 0.1f, 1.f},
                             .scale = 1.f,
                             .visible = true});
        e.set<CombatStats>(CombatStats{team, 8, 8, 3, 1, 80.f});
        e.set<Collider>(Collider{14.f});
        if (out_ids)
            out_ids->push_back(e.id());
    }
}
