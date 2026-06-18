#include "world/map-data.hpp"
#include "world/world-state.hpp"
#include <boost/test/unit_test.hpp>

namespace {
constexpr float kHour = 1.F;
constexpr float kDayHours = 24.F;
constexpr float kStartHour = 6.F;
constexpr int kTestPopulation = 1000;
constexpr float k18Hours = 18.F;
constexpr float kMidnightThreshold = 1.F;
constexpr float kAlmostMidnightLow = 23.9F;
constexpr float kAlmostMidnightHigh = 24.1F;
} // namespace

BOOST_AUTO_TEST_SUITE(world_tests)

BOOST_AUTO_TEST_CASE(initial_state)
{
    WorldState ws;
    BOOST_TEST(ws.day() == 1);
    BOOST_TEST(ws.season() == 0);
    BOOST_TEST(ws.time_of_day() >= kStartHour);
    BOOST_TEST(ws.time_of_day() < kStartHour + kHour);
}

BOOST_AUTO_TEST_CASE(day_advances_with_time)
{
    WorldState ws;
    // Advance one full day
    ws.update(kDayHours + kHour);
    BOOST_TEST(ws.day() == 2);
}

BOOST_AUTO_TEST_CASE(day_advances_multiple_days)
{
    WorldState ws;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    ws.update(kDayHours * 5);
    BOOST_TEST(ws.day() == 6);
}

BOOST_AUTO_TEST_CASE(season_changes_every_90_days)
{
    WorldState ws;
    // Should still be spring (0) after 89 days
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    ws.update(kDayHours * 89);
    BOOST_TEST(ws.season() == 0);

    // Should become summer (1) after 90 days
    ws.update(kDayHours);
    BOOST_TEST(ws.season() == 1);
}

BOOST_AUTO_TEST_CASE(season_cycles_to_spring)
{
    WorldState ws;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    ws.update(kDayHours * 360); // ~360 days
    BOOST_TEST(ws.season() == 0);
}

BOOST_AUTO_TEST_CASE(tile_visibility_default_unseen)
{
    WorldState ws;
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
    BOOST_TEST(!ws.is_tile_explored(Vec2i(0, 0)));
    BOOST_TEST(!ws.is_tile_explored(Vec2i(10, 10)));
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
}

BOOST_AUTO_TEST_CASE(reveal_single_tile)
{
    WorldState ws;
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
    ws.reveal_tile(Vec2i(5, 5));
    BOOST_TEST(ws.is_tile_explored(Vec2i(5, 5)));
    BOOST_TEST(!ws.is_tile_explored(Vec2i(5, 6)));
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
}

BOOST_AUTO_TEST_CASE(reveal_radius)
{
    WorldState ws;
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
    // explore_radius uses world-space coords with circular distance
    constexpr auto kRadius = 3.F;
    ws.explore_radius(center_of_tile(Vec2i(10, 10)), kRadius * tile_size);
    BOOST_TEST(ws.is_tile_explored(Vec2i(10, 10)));
    BOOST_TEST(ws.is_tile_explored(Vec2i(10, 13)));
    BOOST_TEST(ws.is_tile_explored(Vec2i(10, 7)));
    BOOST_TEST(ws.is_tile_explored(Vec2i(13, 10)));
    BOOST_TEST(ws.is_tile_explored(Vec2i(7, 10)));
    // Corner at Euclidean distance sqrt(3²+3²) tile_units ≈ 4.24 > 3 — not explored
    BOOST_TEST(!ws.is_tile_explored(Vec2i(13, 13)));
    // Outside radius
    BOOST_TEST(!ws.is_tile_explored(Vec2i(14, 14)));
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
}

BOOST_AUTO_TEST_CASE(repeat_reveal_is_safe)
{
    WorldState ws;
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
    ws.reveal_tile(Vec2i(0, 0));
    ws.reveal_tile(Vec2i(0, 0)); // Should not crash or change state
    BOOST_TEST(ws.is_tile_explored(Vec2i(0, 0)));
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
}

BOOST_AUTO_TEST_CASE(location_management)
{
    WorldState ws;
    WorldState::LocationState loc;
    loc.name = "Ugarit";
    loc.population = kTestPopulation;
    ws.add_location("ugarit", loc);

    auto const *retrieved = ws.location("ugarit");
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->name == "Ugarit");
    BOOST_TEST(retrieved->population == kTestPopulation);
    BOOST_TEST(!retrieved->player_has_visited);
}

BOOST_AUTO_TEST_CASE(nonexistent_location_returns_null)
{
    WorldState ws;
    BOOST_TEST(ws.location("nonexistent") == nullptr);
}

BOOST_AUTO_TEST_CASE(location_mutable)
{
    WorldState ws;
    ws.add_location("byblos", WorldState::LocationState{});
    auto *loc = ws.location_mutable("byblos");
    BOOST_REQUIRE(loc != nullptr);
    loc->player_has_visited = true;
    BOOST_TEST(ws.location("byblos")->player_has_visited);
}

BOOST_AUTO_TEST_CASE(time_of_day_wraps)
{
    WorldState ws;
    ws.update(k18Hours); // Start at ~6:00, add 18 hours -> ~0:00 next day
    float tod = ws.time_of_day();
    bool nearMidnight = (tod < kMidnightThreshold) ||
                        (tod >= kAlmostMidnightLow && tod <= kAlmostMidnightHigh);
    BOOST_TEST(nearMidnight, "time should be around midnight, got: " << tod);
}

BOOST_AUTO_TEST_SUITE_END()
