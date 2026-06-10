#include <boost/test/unit_test.hpp>
#include "entities/EntityManager.hpp"

BOOST_AUTO_TEST_SUITE(entity_tests)

BOOST_AUTO_TEST_CASE(create_entity) {
    EntityManager em;
    auto id = em.createEntity();
    BOOST_TEST(id != INVALID_ENTITY);
    BOOST_TEST(em.alive(id));
}

BOOST_AUTO_TEST_CASE(destroy_entity) {
    EntityManager em;
    auto id = em.createEntity();
    em.destroyEntity(id);
    BOOST_TEST(!em.alive(id));
}

BOOST_AUTO_TEST_CASE(multiple_entities_have_unique_ids) {
    EntityManager em;
    auto id1 = em.createEntity();
    auto id2 = em.createEntity();
    BOOST_TEST(id1 != id2);
}

BOOST_AUTO_TEST_CASE(add_and_get_component) {
    EntityManager em;
    auto id = em.createEntity();

    Position p;
    p.worldPos = {10.f, 20.f};
    p.tilePos = {1, 2};
    em.addComponent<Position>(id, p);

    auto* retrieved = em.getComponent<Position>(id);
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->worldPos.x == 10.f);
    BOOST_TEST(retrieved->worldPos.y == 20.f);
    BOOST_TEST(retrieved->tilePos.x == 1);
    BOOST_TEST(retrieved->tilePos.y == 2);
}

BOOST_AUTO_TEST_CASE(has_component) {
    EntityManager em;
    auto id = em.createEntity();

    BOOST_TEST(!em.hasComponent<Position>(id));
    em.addComponent<Position>(id, {});
    BOOST_TEST(em.hasComponent<Position>(id));
    BOOST_TEST(!em.hasComponent<Sprite>(id));
}

BOOST_AUTO_TEST_CASE(remove_component) {
    EntityManager em;
    auto id = em.createEntity();
    em.addComponent<Position>(id, {});
    BOOST_TEST(em.hasComponent<Position>(id));

    em.removeComponent<Position>(id);
    BOOST_TEST(!em.hasComponent<Position>(id));
}

BOOST_AUTO_TEST_CASE(multiple_component_types) {
    EntityManager em;
    auto id = em.createEntity();

    em.addComponent<Position>(id, Position{{5.f, 5.f}, {2, 2}});
    em.addComponent<Sprite>(id, Sprite{"tex", {}, {8.f, 8.f}, {1,0,0,1}, 1.f, true});
    em.addComponent<Movement>(id, Movement{{}, {}, 150.f, false});

    BOOST_TEST(em.hasComponent<Position>(id));
    BOOST_TEST(em.hasComponent<Sprite>(id));
    BOOST_TEST(em.hasComponent<Movement>(id));

    BOOST_TEST(em.getComponent<Position>(id)->worldPos.x == 5.f);
    BOOST_TEST(em.getComponent<Sprite>(id)->textureName == "tex");
    BOOST_TEST(em.getComponent<Movement>(id)->speed == 150.f);
}

BOOST_AUTO_TEST_CASE(entities_list) {
    EntityManager em;
    em.createEntity();
    em.createEntity();
    em.createEntity();

    auto all = em.allEntities();
    BOOST_TEST(all.size() == 3u);

    em.destroyEntity(all[0]);
    BOOST_TEST(em.allEntities().size() == 2u);
}

BOOST_AUTO_TEST_CASE(type_erased_pools_dont_cross_contaminate) {
    EntityManager em;
    auto id = em.createEntity();

    em.addComponent<Position>(id, Position{{0.f, 0.f}, {0, 0}});
    em.addComponent<Movement>(id, Movement{{}, {}, 200.f, false});

    BOOST_TEST(em.getComponent<Movement>(id)->speed == 200.f);
    // Position component should still be valid and independent
    BOOST_TEST(em.getComponent<Position>(id)->worldPos.x == 0.f);
}

BOOST_AUTO_TEST_SUITE_END()
