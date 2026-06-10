#include <boost/test/unit_test.hpp>
#include "entities/entity-manager.hpp"
BOOST_AUTO_TEST_SUITE(entity_tests)

BOOST_AUTO_TEST_CASE(create_entity) {
    EntityManager em;
    auto id = em.create_entity();
    BOOST_TEST(id != invalid_entity);
    BOOST_TEST(em.alive(id));
}

BOOST_AUTO_TEST_CASE(destroy_entity) {
    EntityManager em;
    auto id = em.create_entity();
    em.destroy_entity(id);
    BOOST_TEST(!em.alive(id));
}

BOOST_AUTO_TEST_CASE(multiple_entities_have_unique_ids) {
    EntityManager em;
    auto id1 = em.create_entity();
    auto id2 = em.create_entity();
    BOOST_TEST(id1 != id2);
}

BOOST_AUTO_TEST_CASE(add_and_get_component) {
    EntityManager em;
    auto id = em.create_entity();

    Position p;
    p.world_pos = {10.f, 20.f};
    p.tile_pos = {1, 2};
    em.add_component<Position>(id, p);

    auto* retrieved = em.get_component<Position>(id);
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->world_pos.x == 10.f);
    BOOST_TEST(retrieved->world_pos.y == 20.f);
    BOOST_TEST(retrieved->tile_pos.x == 1);
    BOOST_TEST(retrieved->tile_pos.y == 2);
}

BOOST_AUTO_TEST_CASE(has_component) {
    EntityManager em;
    auto id = em.create_entity();

    BOOST_TEST(!em.has_component<Position>(id));
    em.add_component<Position>(id, {});
    BOOST_TEST(em.has_component<Position>(id));
    BOOST_TEST(!em.has_component<Sprite>(id));
}

BOOST_AUTO_TEST_CASE(remove_component) {
    EntityManager em;
    auto id = em.create_entity();
    em.add_component<Position>(id, {});
    BOOST_TEST(em.has_component<Position>(id));

    em.remove_component<Position>(id);
    BOOST_TEST(!em.has_component<Position>(id));
}

BOOST_AUTO_TEST_CASE(multiple_component_types) {
    EntityManager em;
    auto id = em.create_entity();

    em.add_component<Position>(id, Position{{5.f, 5.f}, {2, 2}});
    em.add_component<Sprite>(id, Sprite{"tex", {}, {8.f, 8.f}, {1,0,0,1}, 1.f, true});
    em.add_component<Movement>(id, Movement{{}, {}, 150.f, false});

    BOOST_TEST(em.has_component<Position>(id));
    BOOST_TEST(em.has_component<Sprite>(id));
    BOOST_TEST(em.has_component<Movement>(id));

    BOOST_TEST(em.get_component<Position>(id)->world_pos.x == 5.f);
    BOOST_TEST(em.get_component<Sprite>(id)->texture_name == "tex");
    BOOST_TEST(em.get_component<Movement>(id)->speed == 150.f);
}

BOOST_AUTO_TEST_CASE(entities_list) {
    EntityManager em;
    em.create_entity();
    em.create_entity();
    em.create_entity();

    auto all = em.all_entities();
    BOOST_TEST(all.size() == 3u);

    em.destroy_entity(all[0]);
    BOOST_TEST(em.all_entities().size() == 2u);
}

BOOST_AUTO_TEST_CASE(type_erased_pools_dont_cross_contaminate) {
    EntityManager em;
    auto id = em.create_entity();

    em.add_component<Position>(id, Position{{0.f, 0.f}, {0, 0}});
    em.add_component<Movement>(id, Movement{{}, {}, 200.f, false});

    BOOST_TEST(em.get_component<Movement>(id)->speed == 200.f);
    // Position component should still be valid and independent
    BOOST_TEST(em.get_component<Position>(id)->world_pos.x == 0.f);
}

BOOST_AUTO_TEST_SUITE_END()
