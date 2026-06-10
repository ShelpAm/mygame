#include <boost/test/unit_test.hpp>
#include "systems/CombatSystem.hpp"
#include "entities/EntityManager.hpp"
#include "entities/components/CombatStats.hpp"
#include "entities/components/Position.hpp"
#include "entities/components/SoldierAI.hpp"

BOOST_AUTO_TEST_SUITE(combat_tests)

BOOST_AUTO_TEST_CASE(combat_stats_initialization) {
    CombatStats cs;
    BOOST_TEST(cs.alive);
    BOOST_TEST(cs.hp == 10);
    BOOST_TEST(cs.attack == 2);
}

BOOST_AUTO_TEST_CASE(damage_kills_entity) {
    EntityManager em;
    auto eid = em.createEntity();
    em.addComponent<CombatStats>(eid, CombatStats{
        .team = Team::Player, .maxHp = 10, .hp = 10,
        .attack = 3, .defense = 1, .attackRange = 80.f
    });
    em.addComponent<Position>(eid, Position{{0,0},{0,0}});

    auto eid2 = em.createEntity();
    em.addComponent<CombatStats>(eid2, CombatStats{
        .team = Team::Enemy, .maxHp = 3, .hp = 3,
        .attack = 2, .defense = 0, .attackRange = 80.f
    });
    em.addComponent<Position>(eid2, Position{{50,0},{1,0}});

    CombatSystem csys;
    // Run several updates until someone dies
    for (int i = 0; i < 20; ++i) {
        csys.update(em, 1.f);
    }

    auto* cs1 = em.getComponent<CombatStats>(eid);
    auto* cs2 = em.getComponent<CombatStats>(eid2);
    BOOST_REQUIRE(cs1 != nullptr);
    BOOST_REQUIRE(cs2 != nullptr);
    // At least one should have taken damage
    BOOST_TEST((cs1->hp < 10 || cs2->hp < 3));
}

BOOST_AUTO_TEST_CASE(soldier_ai_follows_leader) {
    EntityManager em;
    auto leader = em.createEntity();
    em.addComponent<Position>(leader, Position{{100,100},{1,1}});

    auto soldier = em.createEntity();
    em.addComponent<Position>(soldier, Position{{0,0},{0,0}});
    em.addComponent<CombatStats>(soldier, CombatStats{
        .team = Team::Player, .maxHp = 10, .hp = 10
    });
    em.addComponent<SoldierAI>(soldier, SoldierAI{
        .followTarget = leader,
        .formationOffset = {32.f, -32.f},
        .followDistance = 16.f
    });

    CombatSystem csys;
    for (int i = 0; i < 30; ++i) {
        csys.update(em, 1.f / 60.f);
    }

    auto* sPos = em.getComponent<Position>(soldier);
    BOOST_REQUIRE(sPos != nullptr);
    // Soldier should have moved toward leader
    BOOST_TEST(sPos->worldPos.x > 20.f);
    BOOST_TEST(sPos->worldPos.y > 20.f);
}

BOOST_AUTO_TEST_CASE(spawn_enemy_wave) {
    EntityManager em;
    CombatSystem csys;
    csys.spawnEnemyWave(em, 3, {0, 0}, 100.f, Team::Enemy);

    int enemyCount = 0;
    for (auto id : em.allEntities()) {
        auto* cs = em.getComponent<CombatStats>(id);
        if (cs && cs->team == Team::Enemy) {
            enemyCount++;
            BOOST_TEST(cs->alive);
        }
    }
    BOOST_TEST(enemyCount == 3);
}

BOOST_AUTO_TEST_CASE(team_near_position) {
    EntityManager em;
    auto eid = em.createEntity();
    em.addComponent<CombatStats>(eid, CombatStats{
        .team = Team::Player, .maxHp = 10, .hp = 10
    });
    em.addComponent<Position>(eid, Position{{50, 0}, {0, 0}});

    CombatSystem csys;
    BOOST_TEST(csys.teamNearPosition(em, Team::Player, {0, 0}, 100.f));
    BOOST_TEST(!csys.teamNearPosition(em, Team::Player, {0, 0}, 10.f));
    BOOST_TEST(!csys.teamNearPosition(em, Team::Enemy, {0, 0}, 200.f));
}

BOOST_AUTO_TEST_CASE(dead_entity_not_in_combat) {
    EntityManager em;
    auto eid = em.createEntity();
    em.addComponent<CombatStats>(eid, CombatStats{
        .team = Team::Player, .maxHp = 10, .hp = 10, .alive = false
    });
    em.addComponent<Position>(eid, Position{{0, 0}, {0, 0}});

    CombatSystem csys;
    BOOST_TEST(!csys.teamNearPosition(em, Team::Player, {0, 0}, 200.f));
}

BOOST_AUTO_TEST_CASE(combat_events_generated) {
    EntityManager em;
    auto eid1 = em.createEntity();
    em.addComponent<CombatStats>(eid1, CombatStats{
        .team = Team::Player, .maxHp = 10, .hp = 10, .attack = 5, .attackRange = 100.f
    });
    em.addComponent<Position>(eid1, Position{{0, 0}, {0, 0}});

    auto eid2 = em.createEntity();
    em.addComponent<CombatStats>(eid2, CombatStats{
        .team = Team::Enemy, .maxHp = 10, .hp = 10, .attack = 3, .attackRange = 100.f
    });
    em.addComponent<Position>(eid2, Position{{50, 0}, {1, 0}});

    CombatSystem csys;
    for (int i = 0; i < 10; ++i) {
        csys.update(em, 1.f);
    }

    auto events = csys.events();
    BOOST_TEST(!events.empty());
}

BOOST_AUTO_TEST_SUITE_END()
