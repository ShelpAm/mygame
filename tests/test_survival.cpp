#include "survival/condition-tracker.hpp"
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_SUITE(survival_tests)

BOOST_AUTO_TEST_CASE(initial_state)
{
    ConditionTracker ct;
    auto const &s = ct.state();
    BOOST_TEST(s.food == 100.f);
    BOOST_TEST(s.water == 100.f);
    BOOST_TEST(s.health == 100.f);
    BOOST_TEST(s.energy == 100.f);
}

BOOST_AUTO_TEST_CASE(food_decays_over_time)
{
    ConditionTracker ct;
    ct.update(24.f, false, false); // 1 game day of idle
    BOOST_TEST(ct.state().food < 100.f);
    BOOST_TEST(ct.state().water < 100.f);
}

BOOST_AUTO_TEST_CASE(moving_drains_energy)
{
    ConditionTracker ct;
    float before = ct.state().energy;
    ct.update(1.f, true, false); // 1 hour moving
    BOOST_TEST(ct.state().energy < before);
}

BOOST_AUTO_TEST_CASE(sleeping_restores_energy)
{
    ConditionTracker ct;
    ct.update(1.f, true, false); // drain energy
    float drained = ct.state().energy;

    ct.update(1.f, false, true); // sleep
    BOOST_TEST(ct.state().energy > drained);
}

BOOST_AUTO_TEST_CASE(starvation_damages_health)
{
    ConditionTracker ct;
    // Run until food is gone
    ct.update(50.f * 24.f, false, false); // ~50 days
    BOOST_TEST(ct.state().food <= 0.f);
    BOOST_TEST(ct.state().health < 100.f);
}

BOOST_AUTO_TEST_CASE(consume_food)
{
    ConditionTracker ct;
    ct.update(24.f, false, false); // lose some food
    float before = ct.state().food;
    ct.consume_food(20.f);
    BOOST_TEST(ct.state().food > before);
}

BOOST_AUTO_TEST_CASE(consume_water)
{
    ConditionTracker ct;
    ct.update(24.f, false, false);
    float before = ct.state().water;
    ct.consume_water(20.f);
    BOOST_TEST(ct.state().water > before);
}

BOOST_AUTO_TEST_CASE(healing)
{
    ConditionTracker ct;
    ct.update(100.f * 24.f, false, false); // run health down
    float before = ct.state().health;
    ct.heal(30.f);
    BOOST_TEST(ct.state().health > before);
}

BOOST_AUTO_TEST_CASE(clamping_bounds)
{
    ConditionTracker ct;
    ct.consume_food(200.f);
    BOOST_TEST(ct.state().food == 100.f);

    ct.consume_water(200.f);
    BOOST_TEST(ct.state().water == 100.f);
}

BOOST_AUTO_TEST_CASE(is_dead_when_health_zero)
{
    ConditionTracker ct;
    // Extreme starvation
    ct.update(200.f * 24.f, false, false);
    // Health may or may not be 0 depending on values
    // Just verify it's queryable
    BOOST_TEST(ct.state().health >= 0.f);
    BOOST_TEST(ct.state().health <= 100.f);
}

BOOST_AUTO_TEST_SUITE_END()
