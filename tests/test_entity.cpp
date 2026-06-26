#include "core/game-types.hpp"
#include "entities/components/building-data.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
#include <boost/test/unit_test.hpp>
#include <flecs.h>

BOOST_AUTO_TEST_SUITE(entity_tests)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

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

    e.set<Transform>(Transform{.world_pos = Vec2f(10.F, 20.F)});

    auto const *retrieved = e.try_get<Transform>();
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->world_pos.x == 10.F);
    BOOST_TEST(retrieved->world_pos.y == 20.F);
}

BOOST_AUTO_TEST_CASE(has_component)
{
    flecs::world world;
    auto e = world.entity();

    BOOST_TEST(!e.has<Transform>());
    e.set<Transform>({});
    BOOST_TEST(e.has<Transform>());
    BOOST_TEST(!e.has<Sprite>());
}

BOOST_AUTO_TEST_CASE(remove_component)
{
    flecs::world world;
    auto e = world.entity();
    e.set<Transform>({});
    BOOST_TEST(e.has<Transform>());

    e.remove<Transform>();
    BOOST_TEST(!e.has<Transform>());
}

BOOST_AUTO_TEST_CASE(multiple_component_types)
{
    flecs::world world;
    auto e = world.entity();

    e.set<Transform>(Transform{.world_pos = Vec2f(5.F, 5.F)});
    e.set<Sprite>(Sprite{.texture_name = "tex",
                         .origin = Vec2f(8.F, 8.F),
                         .color = {.r = 1.F, .g = 1.F, .b = 1.F, .a = 1.F},
                         .scale = 1.F,
                         .visible = true});
    e.set<Movement>(Movement{
        .max_speed = 150.F,
        .velocity = {},
    });

    BOOST_TEST(e.has<Transform>());
    BOOST_TEST(e.has<Sprite>());
    BOOST_TEST(e.has<Movement>());

    BOOST_TEST(e.try_get<Transform>()->world_pos.x == 5.F);
    BOOST_TEST(e.try_get<Sprite>()->texture_name == "tex");
    BOOST_TEST(e.try_get<Movement>()->max_speed == 150.F);
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

    e.set<Transform>(Transform{.world_pos = Vec2f(0.F, 0.F)});
    e.set<Movement>(Movement{
        .max_speed = 200.F,
        .velocity = {},
    });

    BOOST_TEST(e.try_get<Movement>()->max_speed == 200.F);
    BOOST_TEST(e.try_get<Transform>()->world_pos.x == 0.F);
}

// Verify that entities with BuildingData are recognised as structures
// (mimicking the logic in GameMode::entity_kind()).
BOOST_AUTO_TEST_CASE(building_entity_kind_is_structure)
{
    flecs::world world;

    // Entity WITHOUT BuildingData should NOT match.
    auto plain = world.entity();
    BOOST_TEST(!plain.has<BuildingData>());

    // Entity WITH BuildingData should match.
    auto building = world.entity().set(
        BuildingData{BuildingData::Type::inn, "thornhaven", ""});
    BOOST_TEST(building.has<BuildingData>());

    // Simulate the entity_kind logic:
    auto kind_of = [&](flecs::entity e) -> uint8_t {
        if (e.has<BuildingData>()) return 5; // EntityKind::structure
        return 4;                              // EntityKind::enemy (fallback)
    };

    BOOST_TEST(kind_of(plain) == 4);
    BOOST_TEST(kind_of(building) == 5);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_SUITE_END()
