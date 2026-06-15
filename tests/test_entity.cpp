#include "core/game-types.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
#include <boost/test/unit_test.hpp>
#include <flecs.h>
BOOST_AUTO_TEST_SUITE(entity_tests)

BOOST_AUTO_TEST_CASE(create_entity)
{
    flecs::world world;
    auto e = world.entity();
    auto id = e.id();
    BOOST_TEST(id != invalid_entity);
    BOOST_TEST(world.is_alive(id));
}

BOOST_AUTO_TEST_CASE(destroy_entity)
{
    flecs::world world;
    auto e = world.entity();
    auto id = e.id();
    e.destruct();
    BOOST_TEST(!world.is_alive(id));
}

BOOST_AUTO_TEST_CASE(multiple_entities_have_unique_ids)
{
    flecs::world world;
    auto id1 = world.entity().id();
    auto id2 = world.entity().id();
    BOOST_TEST(id1 != id2);
}

BOOST_AUTO_TEST_CASE(add_and_get_component)
{
    flecs::world world;
    auto e = world.entity();

    e.set<Position>(Position{{10.f, 20.f}, {1, 2}});

    auto const *retrieved = e.try_get<Position>();
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->world_pos.x == 10.f);
    BOOST_TEST(retrieved->world_pos.y == 20.f);
    BOOST_TEST(retrieved->tile_pos.x == 1);
    BOOST_TEST(retrieved->tile_pos.y == 2);
}

BOOST_AUTO_TEST_CASE(has_component)
{
    flecs::world world;
    auto e = world.entity();

    BOOST_TEST(!e.has<Position>());
    e.set<Position>({});
    BOOST_TEST(e.has<Position>());
    BOOST_TEST(!e.has<Sprite>());
}

BOOST_AUTO_TEST_CASE(remove_component)
{
    flecs::world world;
    auto e = world.entity();
    e.set<Position>({});
    BOOST_TEST(e.has<Position>());

    e.remove<Position>();
    BOOST_TEST(!e.has<Position>());
}

BOOST_AUTO_TEST_CASE(multiple_component_types)
{
    flecs::world world;
    auto e = world.entity();

    e.set<Position>(Position{{5.f, 5.f}, {2, 2}});
    e.set<Sprite>(Sprite{"tex", {}, {8.f, 8.f}, {1, 0, 0, 1}, 1.f, true});
    e.set<Movement>(Movement{{}, {}, 150.f, false});

    BOOST_TEST(e.has<Position>());
    BOOST_TEST(e.has<Sprite>());
    BOOST_TEST(e.has<Movement>());

    BOOST_TEST(e.try_get<Position>()->world_pos.x == 5.f);
    BOOST_TEST(e.try_get<Sprite>()->texture_name == "tex");
    BOOST_TEST(e.try_get<Movement>()->speed == 150.f);
}

BOOST_AUTO_TEST_CASE(entities_list)
{
    flecs::world world;
    auto e1 = world.entity();
    auto e2 = world.entity();
    auto e3 = world.entity();

    BOOST_TEST(world.is_alive(e1.id()));
    BOOST_TEST(world.is_alive(e2.id()));
    BOOST_TEST(world.is_alive(e3.id()));

    e1.destruct();
    BOOST_TEST(!world.is_alive(e1.id()));
    BOOST_TEST(world.is_alive(e2.id()));
    BOOST_TEST(world.is_alive(e3.id()));
}

BOOST_AUTO_TEST_CASE(type_erased_pools_dont_cross_contaminate)
{
    flecs::world world;
    auto e = world.entity();

    e.set<Position>(Position{{0.f, 0.f}, {0, 0}});
    e.set<Movement>(Movement{{}, {}, 200.f, false});

    BOOST_TEST(e.try_get<Movement>()->speed == 200.f);
    BOOST_TEST(e.try_get<Position>()->world_pos.x == 0.f);
}

BOOST_AUTO_TEST_SUITE_END()
