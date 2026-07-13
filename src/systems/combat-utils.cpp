#include "systems/combat-utils.hpp"
#include "world/map-data.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <spdlog/spdlog.h>

int calc_damage(int attack, int defense)
{
    thread_local std::mt19937 rng{std::random_device{}()};
    int base = std::max(1, attack - defense / 2);
    std::uniform_int_distribution<int> dist(-1, 1);
    int variance = dist(rng);
    return std::max(1, base + variance);
}

EntityId find_nearest_enemy(flecs::world &world, EntityId self,
                            std::unordered_map<EntityId, int> const &extra_damage,
                            float detection_range)
{
    flecs::entity me = world.entity(self);
    auto const &my_pos = me.get<Transform>();
    auto const &my_stats = me.get<CombatStats>();

    EntityId nearest = 0;
    float nearestDist = detection_range > 0.F ? detection_range : std::numeric_limits<float>::max();

    world.query<CombatStats, Transform>().each(
        [&](flecs::entity e, CombatStats &cs, Transform &pos) {
            if (e.id() == self || !cs.alive || !is_hostile(my_stats.team, cs.team))
                return;
            auto it = extra_damage.find(e.id());
            if (it != extra_damage.end() && cs.hp - it->second <= 0)
                return;
            float d = (pos.world_pos - my_pos.world_pos).length();
            if (d < nearestDist) {
                nearestDist = d;
                nearest = e.id();
            }
        });
    return nearest;
}

void decay_survival(SurvivalState &s, float dt)
{
    float days_passed = dt / 24.F;
    s.food -= SurvivalState::food_decay_per_day * days_passed;
    s.water -= SurvivalState::water_decay_per_day * days_passed;
    if (s.food <= 0.F)
        s.health -= SurvivalState::health_decay_starving * days_passed;
    if (s.water <= 0.F)
        s.health -= SurvivalState::health_decay_starving * days_passed;
    s.energy -= 0.5F * dt;
    s.food = std::clamp(s.food, 0.F, 100.F);
    s.water = std::clamp(s.water, 0.F, 100.F);
    s.health = std::clamp(s.health, 0.F, 100.F);
    s.energy = std::clamp(s.energy, 0.F, 100.F);
}

void run_soldier_ai(flecs::world &world, flecs::entity e, SoldierAI &ai, Transform &pos,
                    Movement &mov, CombatStats &cs, NavigationSystem const *nav, float dt,
                    std::function<void(EntityId)> const &mark_dirty)
{
    if (!cs.alive) {
        mov.velocity = {0, 0};
        return;
    }

    assert(nav && "run_soldier_ai: nav pointer is null");

    // ==========================================
    // Phase 1: tactical brain — where am I going? (determine Goal)
    // ==========================================
    Vec2f goal_pos = pos.world_pos; // default: stand still
    bool is_in_combat = false;

    if (ai.stance == SoldierStance::offensive || ai.stance == SoldierStance::defensive) {
        float search_range =
            (ai.stance == SoldierStance::offensive) ? ai.engage_range : cs.attack_range;
        auto enemy = find_nearest_enemy(world, e.id(), {}, search_range);

        if (enemy != 0) {
            goal_pos = world.entity(enemy).get<Transform>().world_pos;
            is_in_combat = true;
        }
    }

    // If not fighting, go back to formation position
    if (!is_in_combat) {
        auto const &leader_pos = e.target<Follows>().get<Transform>();
        goal_pos = leader_pos.world_pos + ai.formation_offset;
    }

    // Update combat state and mark dirty (optimized redundant closure call)
    if (ai.in_combat != is_in_combat) {
        spdlog::debug("SoldierAI {}: combat state change {}→{}", e.id(), ai.in_combat,
                      is_in_combat);
        ai.in_combat = is_in_combat;
        mark_dirty(e.id());
    }

    // If already at destination, stop early
    float dist_to_goal = (goal_pos - pos.world_pos).length();
    if (dist_to_goal <= ai.follow_distance) {
        if (mov.velocity.length() > 0.F)
            spdlog::trace("SoldierAI {}: arrived at goal (dist={:.1f}), stopping", e.id(),
                          dist_to_goal);
        mov.velocity = {0, 0};
        return;
    }

    // ==========================================
    // Phase 2: navigation — A* + path smoothing (determine Waypoint)
    // ==========================================
    Vec2f target_waypoint = goal_pos; // default: go straight to goal

    Vec2i curr_tile = world_to_tile(pos.world_pos);
    Vec2i goal_tile = world_to_tile(goal_pos);
    if (curr_tile != goal_tile) {
        auto path = nav->find_path(curr_tile, goal_tile);
        if (path.empty()) {
            spdlog::trace("SoldierAI {}: A* found no path from ({},{}) to ({},{}), stopping",
                          e.id(), curr_tile.x, curr_tile.y, goal_tile.x, goal_tile.y);
            mov.velocity = {0, 0};
            return;
        }
        // Path smoothing: walk from farthest to nearest, pick the first tile
        // that has a clear line-of-sight from current position.
        bool found = false;
        for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {
            if (nav->walkable_line(curr_tile, path[i])) {
                target_waypoint = center_of_tile(path[i]);
                found = true;
                break;
            }
        }
        if (!found) {
            spdlog::trace(
                "SoldierAI {}: path of {} tiles but no walkable line to any, using first step",
                e.id(), path.size());
            target_waypoint = center_of_tile(path[0]);
        }
    }

    // ==========================================
    // Phase 3: physics — step and turn (output Velocity)
    // ==========================================
    Vec2f diff = target_waypoint - pos.world_pos;
    float dist = diff.length();

    if (dist > 1e-5F) {
        Vec2f desired_velocity = diff.normalized() * mov.max_speed;
        float t = std::min(1.F, dt * 8.F);
        mov.velocity = mov.velocity + (desired_velocity - mov.velocity) * t;
        mark_dirty(e.id());
        spdlog::trace("SoldierAI {}: moving  pos=({:.1f},{:.1f}) "
                      "goal=({:.1f},{:.1f}) dist={:.1f} waypoint=({:.1f},{:.1f}) "
                      "vel=({:.2f},{:.2f})",
                      e.id(), pos.world_pos.x, pos.world_pos.y, goal_pos.x, goal_pos.y, dist,
                      target_waypoint.x, target_waypoint.y, mov.velocity.x, mov.velocity.y);
    }
    else {
        if (mov.velocity.length() > 0.F)
            spdlog::trace("SoldierAI {}: zero diff, stopping", e.id());
        mov.velocity = {0, 0};
    }

    spdlog::trace("SoldierAI: entity {} pos=({:.2f},{:.2f}) "
                  "goal=({:.2f},{:.2f}) goal_reason={} formation_offset=({:.2f},{:.2f}) "
                  "waypoint=({:.2f},{:.2f}) velocity=({:.2f},{:.2f})",
                  e.id(), pos.world_pos.x, pos.world_pos.y, goal_pos.x, goal_pos.y,
                  is_in_combat ? "combat" : "formation", ai.formation_offset.x,
                  ai.formation_offset.y, target_waypoint.x, target_waypoint.y, mov.velocity.x,
                  mov.velocity.y);
}

void run_combat_batch(flecs::world &world, float dt, std::vector<CombatEvent> &out_events,
                      std::vector<Projectile> &out_projectiles,
                      std::function<void(EntityId)> const &mark_dirty)
{
    std::vector<flecs::entity> combatants;
    world.query<CombatStats, Transform>().each([&](flecs::entity e, CombatStats &cs, Transform &) {
        if (cs.alive && cs.team != Team::neutral)
            combatants.push_back(e);
    });

    std::unordered_map<EntityId, int> damage_dealt;
    std::unordered_map<EntityId, float> new_cooldowns;

    for (auto &attacker : combatants) {
        auto const *atkStats = attacker.try_get<CombatStats>();
        auto const *atkPos = attacker.try_get<Transform>();
        if (!atkStats || !atkPos)
            continue;

        float cd = atkStats->cooldown_remaining - dt;
        if (auto it = new_cooldowns.find(attacker.id()); it != new_cooldowns.end())
            cd = it->second;

        if (cd > 0.f) {
            new_cooldowns[attacker.id()] = cd;
            continue;
        }

        EntityId targetId =
            find_nearest_enemy(world, attacker.id(), damage_dealt, atkStats->attack_range);
        if (targetId == 0)
            continue;

        auto target_e = world.entity(targetId);
        auto const *defStats = target_e.try_get<CombatStats>();
        if (!defStats)
            continue;

        int effHp = defStats->hp - damage_dealt[targetId];
        if (effHp <= 0)
            continue;

        int dmg = calc_damage(atkStats->attack, defStats->defense);
        new_cooldowns[attacker.id()] = atkStats->attack_cooldown;

        // Ranged: spawn projectile instead of instant damage
        auto const *ai = attacker.try_get<SoldierAI>();
        if (ai && ai->role == SoldierRole::ranged) {
            auto const *defPos = target_e.try_get<Transform>();
            if (defPos) {
                Projectile p;
                p.pos = atkPos->world_pos;
                p.target_id = targetId;
                p.damage = dmg;
                Vec2f d = defPos->world_pos - p.pos;
                p.total_dist = std::sqrt(d.x * d.x + d.y * d.y);
                p.direction = p.total_dist > 0.f ? Vec2f{d.x / p.total_dist, d.y / p.total_dist}
                                                 : Vec2f{1.f, 0.f};
                // Accuracy: farther = less accurate
                thread_local std::mt19937 hit_rng{std::random_device{}()};
                float hit_chance = std::clamp(0.9f - p.total_dist * 0.002f, 0.35f, 0.95f);
                std::uniform_real_distribution<float> hit_dist(0.f, 1.f);
                if (hit_dist(hit_rng) > hit_chance)
                    p.damage = 0; // miss
                out_projectiles.push_back(p);
                continue;
            }
        }

        // Melee: instant damage
        damage_dealt[targetId] += dmg;
        out_events.push_back({attacker.id(), targetId, dmg, effHp - dmg <= 0});
    }

    for (auto &[eid, cd] : new_cooldowns) {
        auto *cs = world.entity(eid).try_get_mut<CombatStats>();
        if (cs) {
            cs->cooldown_remaining = cd;
            mark_dirty(eid);
        }
    }
    for (auto &[eid, dmg] : damage_dealt) {
        auto *cs = world.entity(eid).try_get_mut<CombatStats>();
        if (cs) {
            cs->hp -= dmg;
            if (cs->hp <= 0) {
                cs->alive = false;
                cs->hp = 0;
            }
            mark_dirty(eid);
        }
    }
}

void update_projectiles(flecs::world &world, std::vector<Projectile> &projectiles, float dt,
                        std::vector<CombatEvent> &out_events)
{
    for (auto it = projectiles.begin(); it != projectiles.end();) {
        float step = it->speed * dt;
        it->pos = it->pos + it->direction * step;
        it->traveled += step;

        if (it->traveled >= it->total_dist) {
            if (it->damage > 0) {
                auto *cs = world.entity(it->target_id).try_get_mut<CombatStats>();
                if (cs && cs->alive) {
                    cs->hp -= it->damage;
                    bool killed = cs->hp <= 0;
                    if (killed) {
                        cs->alive = false;
                        cs->hp = 0;
                    }
                    out_events.push_back({0, it->target_id, it->damage, killed});
                }
            }
            it = projectiles.erase(it);
        }
        else {
            ++it;
        }
    }
}
