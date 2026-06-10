#include "systems/navigation-system.hpp"
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_SUITE(navigation_tests)

BOOST_AUTO_TEST_CASE(empty_grid_path_to_self)
{
    NavigationSystem nav;
    auto path = nav.find_path({0, 0}, {0, 0});
    BOOST_TEST(path.size() == 1u);
}

BOOST_AUTO_TEST_CASE(straight_line_path)
{
    NavigationSystem nav;
    auto path = nav.find_path({0, 0}, {5, 0});
    BOOST_TEST(path.size() == 6u);
    BOOST_TEST(path.front().x == 0);
    BOOST_TEST(path.front().y == 0);
    BOOST_TEST(path.back().x == 5);
    BOOST_TEST(path.back().y == 0);
}

BOOST_AUTO_TEST_CASE(diagonal_equivalent_path)
{
    NavigationSystem nav;
    auto path = nav.find_path({0, 0}, {2, 2});
    BOOST_TEST(path.size() == 5u);
    BOOST_TEST(path.back().x == 2);
    BOOST_TEST(path.back().y == 2);
}

BOOST_AUTO_TEST_CASE(blocked_goal_returns_empty)
{
    NavigationSystem nav;
    nav.set_walkable({1, 1}, false);
    auto path = nav.find_path({0, 0}, {1, 1});
    BOOST_TEST(path.empty());
}

BOOST_AUTO_TEST_CASE(path_around_obstacle)
{
    NavigationSystem nav;
    nav.set_walkable({1, 0}, false);
    nav.set_walkable({1, 1}, false);

    auto path = nav.find_path({0, 0}, {2, 0});
    BOOST_TEST(!path.empty());
    BOOST_TEST(path.back().x == 2);
    BOOST_TEST(path.back().y == 0);
}

BOOST_AUTO_TEST_CASE(no_path_through_complete_blockage)
{
    NavigationSystem nav;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0)
                continue;
            nav.set_walkable({dx, dy}, false);
        }
    }
    auto path = nav.find_path({0, 0}, {5, 5});
    BOOST_TEST(path.empty());
}

BOOST_AUTO_TEST_CASE(walkable_default)
{
    NavigationSystem nav;
    BOOST_TEST(nav.is_walkable({100, 100}));
    BOOST_TEST(nav.is_walkable({-5, 10}));
}

BOOST_AUTO_TEST_CASE(set_unwalkable)
{
    NavigationSystem nav;
    nav.set_walkable({3, 3}, false);
    BOOST_TEST(!nav.is_walkable({3, 3}));

    nav.set_walkable({3, 3}, true);
    BOOST_TEST(nav.is_walkable({3, 3}));
}

BOOST_AUTO_TEST_CASE(longer_path)
{
    NavigationSystem nav;
    auto path = nav.find_path({0, 0}, {10, 5});
    BOOST_TEST(!path.empty());
    BOOST_TEST(path.back().x == 10);
    BOOST_TEST(path.back().y == 5);
}

BOOST_AUTO_TEST_SUITE_END()
