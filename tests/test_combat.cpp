#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/entity-manager.hpp"
#include "systems/combat-system.hpp"
#include <boost/test/unit_test.hpp>
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
    EntityManager em;
    auto eid = em.create_entity();
    em.add_component<CombatStats>(eid, CombatStats{.team = Team::player,
                                                   .max_hp = 10,
                                                   .hp = 10,
                                                   .attack = 3,
                                                   .defense = 1,
                                                   .attack_range = 80.f});
    em.add_component<Position>(eid, Position{{0, 0}, {0, 0}});

    auto eid2 = em.create_entity();
    em.add_component<CombatStats>(eid2, CombatStats{.team = Team::enemy,
                                                    .max_hp = 3,
                                                    .hp = 3,
                                                    .attack = 2,
                                                    .defense = 0,
                                                    .attack_range = 80.f});
    em.add_component<Position>(eid2, Position{{50, 0}, {1, 0}});

    CombatSystem csys;
    // Run several updates until someone dies
    for (int i = 0; i < 20; ++i) {
        csys.update(em, 1.f);
    }

    auto *cs1 = em.get_component<CombatStats>(eid);
    auto *cs2 = em.get_component<CombatStats>(eid2);
    BOOST_REQUIRE(cs1 != nullptr);
    BOOST_REQUIRE(cs2 != nullptr);
    // At least one should have taken damage
    BOOST_TEST((cs1->hp < 10 || cs2->hp < 3));
}

BOOST_AUTO_TEST_CASE(soldier_ai_follows_leader)
{
    EntityManager em;
    auto leader = em.create_entity();
    em.add_component<Position>(leader, Position{{100, 100}, {1, 1}});

    auto soldier = em.create_entity();
    em.add_component<Position>(soldier, Position{{0, 0}, {0, 0}});
    em.add_component<CombatStats>(
        soldier, CombatStats{.team = Team::player, .max_hp = 10, .hp = 10});
    em.add_component<SoldierAI>(soldier,
                                SoldierAI{.follow_target = leader,
                                          .formation_offset = {32.f, -32.f},
                                          .follow_distance = 16.f});

    CombatSystem csys;
    for (int i = 0; i < 30; ++i) {
        csys.update(em, 1.f / 60.f);
    }

    auto *sPos = em.get_component<Position>(soldier);
    BOOST_REQUIRE(sPos != nullptr);
    // Soldier should have moved toward leader
    BOOST_TEST(sPos->world_pos.x > 20.f);
    BOOST_TEST(sPos->world_pos.y > 20.f);
}

BOOST_AUTO_TEST_CASE(spawn_enemy_wave)
{
    EntityManager em;
    CombatSystem csys;
    csys.spawn_enemy_wave(em, 3, {0, 0}, 100.f, Team::enemy);

    int enemyCount = 0;
    for (auto id : em.all_entities()) {
        auto *cs = em.get_component<CombatStats>(id);
        if (cs && cs->team == Team::enemy) {
            enemyCount++;
            BOOST_TEST(cs->alive);
        }
    }
    BOOST_TEST(enemyCount == 3);
}

BOOST_AUTO_TEST_CASE(team_near_position)
{
    EntityManager em;
    auto eid = em.create_entity();
    em.add_component<CombatStats>(
        eid, CombatStats{.team = Team::player, .max_hp = 10, .hp = 10});
    em.add_component<Position>(eid, Position{{50, 0}, {0, 0}});

    CombatSystem csys;
    BOOST_TEST(csys.team_near_position(em, Team::player, {0, 0}, 100.f));
    BOOST_TEST(!csys.team_near_position(em, Team::player, {0, 0}, 10.f));
    BOOST_TEST(!csys.team_near_position(em, Team::enemy, {0, 0}, 200.f));
}

BOOST_AUTO_TEST_CASE(dead_entity_not_in_combat)
{
    EntityManager em;
    auto eid = em.create_entity();
    em.add_component<CombatStats>(
        eid, CombatStats{
                 .team = Team::player, .max_hp = 10, .hp = 10, .alive = false});
    em.add_component<Position>(eid, Position{{0, 0}, {0, 0}});

    CombatSystem csys;
    BOOST_TEST(!csys.team_near_position(em, Team::player, {0, 0}, 200.f));
}

BOOST_AUTO_TEST_CASE(combat_events_generated)
{
    EntityManager em;
    auto eid1 = em.create_entity();
    em.add_component<CombatStats>(eid1, CombatStats{.team = Team::player,
                                                    .max_hp = 10,
                                                    .hp = 10,
                                                    .attack = 5,
                                                    .attack_range = 100.f});
    em.add_component<Position>(eid1, Position{{0, 0}, {0, 0}});

    auto eid2 = em.create_entity();
    em.add_component<CombatStats>(eid2, CombatStats{.team = Team::enemy,
                                                    .max_hp = 10,
                                                    .hp = 10,
                                                    .attack = 3,
                                                    .attack_range = 100.f});
    em.add_component<Position>(eid2, Position{{50, 0}, {1, 0}});

    CombatSystem csys;
    for (int i = 0; i < 10; ++i) {
        csys.update(em, 1.f);
    }

    auto events = csys.events();
    BOOST_TEST(!events.empty());
}

BOOST_AUTO_TEST_SUITE_END()
