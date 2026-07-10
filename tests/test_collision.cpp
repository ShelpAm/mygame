#include "systems/collision-system.hpp"
#include "systems/navigation-system.hpp"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(collision_tests)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_CASE(resolve_returns_same_pos_when_all_walkable)
{
    NavigationSystem nav;
    CollisionSystem cs;
    cs.set_navigation(&nav);

    // All tiles walkable by default → position unchanged
    Vec2f pos(100.F, 100.F);
    Vec2f resolved = cs.resolve_tile_collisions(pos, {{-16.F, -16.F}, {16.F, 16.F}});
    BOOST_TEST(resolved.x == pos.x);
    BOOST_TEST(resolved.y == pos.y);
}

BOOST_AUTO_TEST_CASE(resolve_pushes_away_from_blocked_tile)
{
    NavigationSystem nav;
    nav.set_walkable(Vec2i(1, 1), false); // tile (64,64)–(128,128) blocked

    CollisionSystem cs;
    cs.set_navigation(&nav);

    // Position (80,80) with AABB {64–96, 64–96} overlaps blocked tile (1,1)
    Vec2f pos(80.F, 80.F);
    Vec2f resolved = cs.resolve_tile_collisions(pos, {{-16.F, -16.F}, {16.F, 16.F}});

    // Must be pushed away
    bool was_pushed = (resolved.x != pos.x) || (resolved.y != pos.y);
    BOOST_TEST(was_pushed);
}

BOOST_AUTO_TEST_CASE(resolve_handles_large_collider)
{
    NavigationSystem nav;
    nav.set_walkable(Vec2i(0, 0), false);
    nav.set_walkable(Vec2i(1, 0), false);

    CollisionSystem cs;
    cs.set_navigation(&nav);

    Vec2f pos(10.F, 10.F);
    Vec2f resolved = cs.resolve_tile_collisions(pos, {{-32.F, -32.F}, {32.F, 32.F}});

    bool was_pushed = (resolved.x != pos.x) || (resolved.y != pos.y);
    BOOST_TEST(was_pushed);
}

BOOST_AUTO_TEST_CASE(resolve_pushes_from_inside_blocked_tile)
{
    NavigationSystem nav;
    nav.set_walkable(Vec2i(0, 0), false);

    CollisionSystem cs;
    cs.set_navigation(&nav);

    Vec2f pos(20.F, 20.F);
    Vec2f resolved = cs.resolve_tile_collisions(pos, {{-10.F, -10.F}, {10.F, 10.F}});

    // Inside blocked tile → pushed out
    bool was_pushed = (resolved.x != pos.x) || (resolved.y != pos.y);
    BOOST_TEST(was_pushed);
}

BOOST_AUTO_TEST_CASE(resolve_does_not_skip_tile_boundary)
{
    NavigationSystem nav;
    nav.set_walkable(Vec2i(2, 0), false); // isolated blocked tile

    CollisionSystem cs;
    cs.set_navigation(&nav);

    // Position far from the blocked tile — should be unchanged
    Vec2f pos(800.F, 800.F);
    Vec2f resolved = cs.resolve_tile_collisions(pos, {{-16.F, -16.F}, {16.F, 16.F}});
    BOOST_TEST(resolved.x == pos.x);
    BOOST_TEST(resolved.y == pos.y);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_SUITE_END()
