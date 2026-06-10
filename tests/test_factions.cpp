#include <boost/test/unit_test.hpp>
#include "factions/faction-network.hpp"
BOOST_AUTO_TEST_SUITE(faction_tests)

BOOST_AUTO_TEST_CASE(add_and_get_faction) {
    FactionNetwork fn;
    Faction f;
    f.id = "ugarit";
    f.name = "Ugarit Merchants";
    f.power = 60;
    fn.add_faction(f);

    auto* retrieved = fn.get_faction("ugarit");
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->name == "Ugarit Merchants");
    BOOST_TEST(retrieved->power == 60);
}

BOOST_AUTO_TEST_CASE(nonexistent_faction) {
    FactionNetwork fn;
    BOOST_TEST(fn.get_faction("none") == nullptr);
}

BOOST_AUTO_TEST_CASE(faction_relations) {
    FactionNetwork fn;
    fn.add_faction({"a", "Alpha", 50});
    fn.add_faction({"b", "Beta", 50});

    fn.set_relation("a", "b", 80);
    BOOST_TEST(fn.get_relation("a", "b") == 80);
    BOOST_TEST(fn.get_relation("b", "a") == 80);  // symmetric
}

BOOST_AUTO_TEST_CASE(relation_clamping) {
    FactionNetwork fn;
    fn.add_faction({"a", "A", 50});
    fn.add_faction({"b", "B", 50});

    fn.set_relation("a", "b", 150);
    BOOST_TEST(fn.get_relation("a", "b") == 100);

    fn.set_relation("a", "b", -200);
    BOOST_TEST(fn.get_relation("a", "b") == -100);
}

BOOST_AUTO_TEST_CASE(modify_power) {
    FactionNetwork fn;
    fn.add_faction({"a", "A", 40});
    fn.modify_power("a", 20);
    BOOST_TEST(fn.get_faction("a")->power == 60);

    fn.modify_power("a", -80);
    BOOST_TEST(fn.get_faction("a")->power == 0);

    fn.modify_power("a", 200);
    BOOST_TEST(fn.get_faction("a")->power == 100);
}

BOOST_AUTO_TEST_CASE(modify_cohesion_wealth) {
    FactionNetwork fn;
    fn.add_faction({"a", "A", 50, 50, 50});
    fn.modify_cohesion("a", -30);
    BOOST_TEST(fn.get_faction("a")->cohesion == 20);
    fn.modify_wealth("a", 25);
    BOOST_TEST(fn.get_faction("a")->wealth == 75);
}

BOOST_AUTO_TEST_CASE(apply_event_power_shift) {
    FactionNetwork fn;
    fn.add_faction({"a", "A", 50, 50, 50});
    fn.add_faction({"b", "B", 50, 50, 50});

    fn.apply_event("a", 20, "b");
    BOOST_TEST(fn.get_faction("a")->power == 70);
    BOOST_TEST(fn.get_faction("b")->power == 40);
}

BOOST_AUTO_TEST_CASE(apply_event_with_allies) {
    FactionNetwork fn;
    fn.add_faction({"a", "A", 50});
    fn.add_faction({"b", "B", 50});
    fn.add_faction({"c", "C", 50});
    fn.set_relation("b", "c", 70);  // c is b's ally

    fn.apply_event("a", 20, "b");
    // a gains 20
    BOOST_TEST(fn.get_faction("a")->power == 70);
    // b loses 10
    BOOST_TEST(fn.get_faction("b")->power == 40);
    // c (b's ally) loses 5 and relation with a decreases
    BOOST_TEST(fn.get_faction("c")->power == 45);
    BOOST_TEST(fn.get_relation("a", "c") < 0);
}

BOOST_AUTO_TEST_CASE(all_faction_ids) {
    FactionNetwork fn;
    fn.add_faction({"a"});
    fn.add_faction({"b"});
    fn.add_faction({"c"});
    auto ids = fn.all_faction_ids();
    BOOST_TEST(ids.size() == 3u);
}

BOOST_AUTO_TEST_SUITE_END()
