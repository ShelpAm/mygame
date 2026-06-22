#include "systems/combat-system.hpp"
#include "entities/components/collider.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"

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

void CombatSystem::spawn_enemy_wave(flecs::world &world, int count, Vec2f center, float spread,
                                    Team team, std::vector<EntityId> *out_ids)
{
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> angle_dist(0.f, 2.f * std::numbers::pi_v<float>);
    std::uniform_real_distribution<float> dist_dist(0.f, spread);

    for (int i = 0; i < count; ++i) {
        auto e = world.entity();
        float ang = angle_dist(rng);
        float dist = dist_dist(rng);
        float x = center.x + std::cos(ang) * dist;
        float y = center.y + std::sin(ang) * dist;

        e.set<Transform>(Transform{{x, y}});

        e.set<CombatStats>(CombatStats{
            .team = team, .max_hp = 8, .hp = 8, .attack = 3, .defense = 1, .attack_range = 80.F});
        e.set<Collider>(Collider{14.f});
        if (out_ids)
            out_ids->push_back(e.id());
    }
}
