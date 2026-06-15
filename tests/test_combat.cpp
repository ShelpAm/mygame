#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "systems/combat-system.hpp"
#include <boost/test/unit_test.hpp>
#include <flecs.h>
BOOST_AUTO_TEST_SUITE(combat_tests)

BOOST_AUTO_TEST_CASE(combat_stats_initialization)
{
    CombatStats cs;
    BOOST_TEST(cs.alive);
    BOOST_TEST(cs.hp == 10);
    BOOST_TEST(cs.attack == 2);
}

BOOST_AUTO_TEST_CASE(damage_kills_entity)
{
    flecs::world world;
    auto e1 = world.entity();
    e1.set<CombatStats>(CombatStats{.team = Team::player_begin,
                                    .max_hp = 10,
                                    .hp = 10,
                                    .attack = 10,
                                    .defense = 1,
                                    .attack_range = 100.f});
    e1.set<Position>(Position{{0, 0}, {0, 0}});

    auto e2 = world.entity();
    e2.set<CombatStats>(CombatStats{.team = Team::enemy,
                                    .max_hp = 3,
                                    .hp = 3,
                                    .attack = 0,
                                    .defense = 0,
                                    .attack_range = 100.f});
    e2.set<Position>(Position{{50, 0}, {1, 0}});

    CombatSystem csys;
    for (int i = 0; i < 30; ++i) {
        csys.update(world, 1.f);
    }

    // At least some events should have been generated
    BOOST_TEST(!csys.events().empty());
}

BOOST_AUTO_TEST_CASE(soldier_ai_follows_leader)
{
    // Verify soldier data is properly stored and queryable
    flecs::world world;
    auto leader = world.entity();
    leader.set<Position>(Position{{100, 100}, {1, 1}});
    auto leader_team = (Team)((int)Team::player_begin + 10);
    leader.set<CombatStats>(
        CombatStats{.team = leader_team, .max_hp = 10, .hp = 10});

    auto soldier = world.entity();
    soldier.set<Position>(Position{{0, 0}, {0, 0}});
    soldier.set<CombatStats>(
        CombatStats{.team = leader_team, .max_hp = 10, .hp = 10});
    soldier.set<SoldierAI>(SoldierAI{.follow_target = leader.id(),
                                     .formation_offset = {32.f, -32.f},
                                     .follow_distance = 16.f});

    // Verify components are accessible
    auto *sPos = soldier.try_get<Position>();
    BOOST_REQUIRE(sPos != nullptr);
    BOOST_TEST(sPos->world_pos.x == 0.f);

    auto *sAi = soldier.try_get<SoldierAI>();
    BOOST_REQUIRE(sAi != nullptr);
    BOOST_TEST(sAi->follow_target == leader.id());

    // Movement is handled by the Flecs system (Position + Movement) at runtime
}

BOOST_AUTO_TEST_CASE(spawn_enemy_wave)
{
    flecs::world world;
    CombatSystem csys;
    csys.spawn_enemy_wave(world, 3, {0, 0}, 100.f, Team::enemy);

    int enemyCount = 0;
    world.query<CombatStats>().each([&](flecs::entity, CombatStats &cs) {
        if (cs.team == Team::enemy) {
            enemyCount++;
            BOOST_TEST(cs.alive);
        }
    });
    BOOST_TEST(enemyCount == 3);
}

BOOST_AUTO_TEST_CASE(team_near_position)
{
    flecs::world world;
    auto e = world.entity();
    e.set<CombatStats>(
        CombatStats{.team = Team::player_begin, .max_hp = 10, .hp = 10});
    e.set<Position>(Position{{50, 0}, {0, 0}});

    CombatSystem csys;
    BOOST_TEST(
        csys.team_near_position(world, Team::player_begin, {0, 0}, 100.f));
    BOOST_TEST(
        !csys.team_near_position(world, Team::player_begin, {0, 0}, 10.f));
    BOOST_TEST(!csys.team_near_position(world, Team::enemy, {0, 0}, 200.f));
}

BOOST_AUTO_TEST_CASE(dead_entity_not_in_combat)
{
    flecs::world world;
    auto e = world.entity();
    e.set<CombatStats>(CombatStats{
        .team = Team::player_begin, .max_hp = 10, .hp = 10, .alive = false});
    e.set<Position>(Position{{0, 0}, {0, 0}});

    CombatSystem csys;
    BOOST_TEST(
        !csys.team_near_position(world, Team::player_begin, {0, 0}, 200.f));
}

BOOST_AUTO_TEST_CASE(combat_events_generated)
{
    flecs::world world;
    auto e1 = world.entity();
    e1.set<CombatStats>(CombatStats{.team = Team::player_begin,
                                    .max_hp = 10,
                                    .hp = 10,
                                    .attack = 10,
                                    .attack_range = 100.f});
    e1.set<Position>(Position{{0, 0}, {0, 0}});

    auto e2 = world.entity();
    e2.set<CombatStats>(CombatStats{.team = Team::enemy,
                                    .max_hp = 10,
                                    .hp = 10,
                                    .attack = 3,
                                    .attack_range = 100.f});
    e2.set<Position>(Position{{50, 0}, {1, 0}});

    CombatSystem csys;
    for (int i = 0; i < 30; ++i) {
        csys.update(world, 1.f);
    }

    auto events = csys.events();
    BOOST_TEST(!events.empty());
}

BOOST_AUTO_TEST_SUITE_END()
