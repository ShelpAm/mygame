#include <boost/test/unit_test.hpp>
#include "factions/FactionNetwork.hpp"

BOOST_AUTO_TEST_SUITE(faction_tests)

BOOST_AUTO_TEST_CASE(add_and_get_faction) {
    FactionNetwork fn;
    Faction f;
    f.id = "ugarit";
    f.name = "Ugarit Merchants";
    f.power = 60;
    fn.addFaction(f);

    auto* retrieved = fn.getFaction("ugarit");
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->name == "Ugarit Merchants");
    BOOST_TEST(retrieved->power == 60);
}

BOOST_AUTO_TEST_CASE(nonexistent_faction) {
    FactionNetwork fn;
    BOOST_TEST(fn.getFaction("none") == nullptr);
}

BOOST_AUTO_TEST_CASE(faction_relations) {
    FactionNetwork fn;
    fn.addFaction({"a", "Alpha", 50});
    fn.addFaction({"b", "Beta", 50});

    fn.setRelation("a", "b", 80);
    BOOST_TEST(fn.getRelation("a", "b") == 80);
    BOOST_TEST(fn.getRelation("b", "a") == 80);  // symmetric
}

BOOST_AUTO_TEST_CASE(relation_clamping) {
    FactionNetwork fn;
    fn.addFaction({"a", "A", 50});
    fn.addFaction({"b", "B", 50});

    fn.setRelation("a", "b", 150);
    BOOST_TEST(fn.getRelation("a", "b") == 100);

    fn.setRelation("a", "b", -200);
    BOOST_TEST(fn.getRelation("a", "b") == -100);
}

BOOST_AUTO_TEST_CASE(modify_power) {
    FactionNetwork fn;
    fn.addFaction({"a", "A", 40});
    fn.modifyPower("a", 20);
    BOOST_TEST(fn.getFaction("a")->power == 60);

    fn.modifyPower("a", -80);
    BOOST_TEST(fn.getFaction("a")->power == 0);

    fn.modifyPower("a", 200);
    BOOST_TEST(fn.getFaction("a")->power == 100);
}

BOOST_AUTO_TEST_CASE(modify_cohesion_wealth) {
    FactionNetwork fn;
    fn.addFaction({"a", "A", 50, 50, 50});
    fn.modifyCohesion("a", -30);
    BOOST_TEST(fn.getFaction("a")->cohesion == 20);
    fn.modifyWealth("a", 25);
    BOOST_TEST(fn.getFaction("a")->wealth == 75);
}

BOOST_AUTO_TEST_CASE(apply_event_power_shift) {
    FactionNetwork fn;
    fn.addFaction({"a", "A", 50, 50, 50});
    fn.addFaction({"b", "B", 50, 50, 50});

    fn.applyEvent("a", 20, "b");
    BOOST_TEST(fn.getFaction("a")->power == 70);
    BOOST_TEST(fn.getFaction("b")->power == 40);
}

BOOST_AUTO_TEST_CASE(apply_event_with_allies) {
    FactionNetwork fn;
    fn.addFaction({"a", "A", 50});
    fn.addFaction({"b", "B", 50});
    fn.addFaction({"c", "C", 50});
    fn.setRelation("b", "c", 70);  // c is b's ally

    fn.applyEvent("a", 20, "b");
    // a gains 20
    BOOST_TEST(fn.getFaction("a")->power == 70);
    // b loses 10
    BOOST_TEST(fn.getFaction("b")->power == 40);
    // c (b's ally) loses 5 and relation with a decreases
    BOOST_TEST(fn.getFaction("c")->power == 45);
    BOOST_TEST(fn.getRelation("a", "c") < 0);
}

BOOST_AUTO_TEST_CASE(all_faction_ids) {
    FactionNetwork fn;
    fn.addFaction({"a"});
    fn.addFaction({"b"});
    fn.addFaction({"c"});
    auto ids = fn.allFactionIds();
    BOOST_TEST(ids.size() == 3u);
}

BOOST_AUTO_TEST_SUITE_END()
