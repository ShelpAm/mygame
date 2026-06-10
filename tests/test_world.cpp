#include <boost/test/unit_test.hpp>
#include "world/world-state.hpp"
BOOST_AUTO_TEST_SUITE(world_tests)

BOOST_AUTO_TEST_CASE(initial_state) {
    WorldState ws;
    BOOST_TEST(ws.day() == 1);
    BOOST_TEST(ws.season() == 0);
    BOOST_TEST(ws.time_of_day() >= 6.f);
    BOOST_TEST(ws.time_of_day() < 7.f);
}

BOOST_AUTO_TEST_CASE(day_advances_with_time) {
    WorldState ws;
    // Advance one full day
    ws.update(25.f);
    BOOST_TEST(ws.day() == 2);
}

BOOST_AUTO_TEST_CASE(day_advances_multiple_days) {
    WorldState ws;
    ws.update(24.f * 5);
    BOOST_TEST(ws.day() == 6);
}

BOOST_AUTO_TEST_CASE(season_changes_every_90_days) {
    WorldState ws;
    // Should still be spring (0) after 89 days
    ws.update(24.f * 89);
    BOOST_TEST(ws.season() == 0);

    // Should become summer (1) after 90 days
    ws.update(24.f);
    BOOST_TEST(ws.season() == 1);
}

BOOST_AUTO_TEST_CASE(season_cycles_to_spring) {
    WorldState ws;
    ws.update(24.f * 360); // ~360 days
    BOOST_TEST(ws.season() == 0);
}

BOOST_AUTO_TEST_CASE(tile_visibility_default_unseen) {
    WorldState ws;
    BOOST_TEST(!ws.is_tile_seen({0, 0}));
    BOOST_TEST(!ws.is_tile_seen({10, 10}));
}

BOOST_AUTO_TEST_CASE(reveal_single_tile) {
    WorldState ws;
    ws.reveal_tile({5, 5});
    BOOST_TEST(ws.is_tile_seen({5, 5}));
    BOOST_TEST(!ws.is_tile_seen({5, 6}));
}

BOOST_AUTO_TEST_CASE(reveal_radius) {
    WorldState ws;
    ws.reveal_radius({10, 10}, 3);
    BOOST_TEST(ws.is_tile_seen({10, 10}));
    BOOST_TEST(ws.is_tile_seen({10, 13}));
    BOOST_TEST(ws.is_tile_seen({10, 7}));
    BOOST_TEST(ws.is_tile_seen({13, 10}));
    BOOST_TEST(ws.is_tile_seen({7, 10}));
    // Corner of radius 3
    BOOST_TEST(ws.is_tile_seen({13, 13}));
    // Outside radius
    BOOST_TEST(!ws.is_tile_seen({14, 14}));
}

BOOST_AUTO_TEST_CASE(repeat_reveal_is_safe) {
    WorldState ws;
    ws.reveal_tile({0, 0});
    ws.reveal_tile({0, 0});  // Should not crash or change state
    BOOST_TEST(ws.is_tile_seen({0, 0}));
}

BOOST_AUTO_TEST_CASE(location_management) {
    WorldState ws;
    WorldState::LocationState loc;
    loc.name = "Ugarit";
    loc.population = 1000;
    ws.add_location("ugarit", loc);

    const auto* retrieved = ws.location("ugarit");
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->name == "Ugarit");
    BOOST_TEST(retrieved->population == 1000);
    BOOST_TEST(!retrieved->player_has_visited);
}

BOOST_AUTO_TEST_CASE(nonexistent_location_returns_null) {
    WorldState ws;
    BOOST_TEST(ws.location("nonexistent") == nullptr);
}

BOOST_AUTO_TEST_CASE(location_mutable) {
    WorldState ws;
    ws.add_location("byblos", WorldState::LocationState{});
    auto* loc = ws.location_mutable("byblos");
    BOOST_REQUIRE(loc != nullptr);
    loc->player_has_visited = true;
    BOOST_TEST(ws.location("byblos")->player_has_visited);
}

BOOST_AUTO_TEST_CASE(time_of_day_wraps) {
    WorldState ws;
    ws.update(18.f); // Start at ~6:00, add 18 hours -> ~0:00 next day
    float tod = ws.time_of_day();
    bool nearMidnight = (tod < 1.f) || (tod >= 23.9f && tod <= 24.1f);
    BOOST_TEST(nearMidnight, "time should be around midnight, got: " << tod);
}

BOOST_AUTO_TEST_SUITE_END()
