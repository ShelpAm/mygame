#include <boost/test/unit_test.hpp>
#include "knowledge/knowledge-graph.hpp"
BOOST_AUTO_TEST_SUITE(knowledge_tests)

BOOST_AUTO_TEST_CASE(add_and_retrieve_fact) {
    KnowledgeGraph kg;
    Fact f;
    f.id = "test_event_001";
    f.description = "Ugarit was sacked";
    f.type = Fact::Type::event;
    kg.add_or_update_fact(f);

    BOOST_TEST(kg.has_fact("test_event_001"));
    const Fact* retrieved = kg.fact("test_event_001");
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->id == "test_event_001");
    BOOST_TEST(retrieved->description == "Ugarit was sacked");
}

BOOST_AUTO_TEST_CASE(merging_origins_upgrades_certainty) {
    KnowledgeGraph kg;

    Fact f1;
    f1.id = "event";
    f1.origins.push_back({Fact::Origin::Source::npc_testimony, "npc_1", "city_a", 1, 70});
    kg.add_or_update_fact(f1);

    Fact f2;
    f2.id = "event";
    f2.origins.push_back({Fact::Origin::Source::npc_testimony, "npc_2", "city_b", 2, 60});
    kg.add_or_update_fact(f2);

    Fact f3;
    f3.id = "event";
    f3.origins.push_back({Fact::Origin::Source::artifact_read, "", "city_c", 3, 80});
    kg.add_or_update_fact(f3);

    const Fact* f = kg.fact("event");
    BOOST_REQUIRE(f != nullptr);
    BOOST_TEST(f->origins.size() == 3u);
    BOOST_TEST(f->player_certainty == Fact::Certainty::plausible);
}

BOOST_AUTO_TEST_CASE(player_witness_fact) {
    KnowledgeGraph kg;
    Fact f;
    f.id = "witnessed";
    f.origins.push_back({Fact::Origin::Source::player_witness, "", "here", 10, 100});
    kg.add_or_update_fact(f);

    auto witnessed = kg.facts_witnessed();
    BOOST_TEST(witnessed.size() == 1u);

    auto heard = kg.facts_heard();
    BOOST_TEST(heard.size() == 0u);
}

BOOST_AUTO_TEST_CASE(heard_only_fact) {
    KnowledgeGraph kg;
    Fact f;
    f.id = "heard";
    f.origins.push_back({Fact::Origin::Source::npc_testimony, "merchant", "port", 5, 40});
    kg.add_or_update_fact(f);

    auto witnessed = kg.facts_witnessed();
    BOOST_TEST(witnessed.size() == 0u);

    auto heard = kg.facts_heard();
    BOOST_TEST(heard.size() == 1u);
}

BOOST_AUTO_TEST_CASE(fact_relations) {
    KnowledgeGraph kg;
    kg.add_relation("fact_a", "fact_b");

    auto relatedA = kg.related_facts("fact_a");
    BOOST_TEST(relatedA.size() == 1u);
    BOOST_TEST(relatedA[0] == "fact_b");

    auto relatedB = kg.related_facts("fact_b");
    BOOST_TEST(relatedB.size() == 1u);
    BOOST_TEST(relatedB[0] == "fact_a");

    auto unrelated = kg.related_facts("fact_c");
    BOOST_TEST(unrelated.empty());
}

BOOST_AUTO_TEST_CASE(topic_tracking) {
    KnowledgeGraph kg;
    BOOST_TEST(!kg.is_topic_known("ugarit_sack"));

    kg.mark_topic_known("ugarit_sack");
    BOOST_TEST(kg.is_topic_known("ugarit_sack"));
}

BOOST_AUTO_TEST_CASE(total_facts_count) {
    KnowledgeGraph kg;
    BOOST_TEST(kg.total_facts() == 0u);

    Fact f1; f1.id = "a"; kg.add_or_update_fact(f1);
    Fact f2; f2.id = "b"; kg.add_or_update_fact(f2);
    BOOST_TEST(kg.total_facts() == 2u);

    // Same ID should merge, not add
    Fact f3; f3.id = "a"; kg.add_or_update_fact(f3);
    BOOST_TEST(kg.total_facts() == 2u);
}

BOOST_AUTO_TEST_CASE(fact_types) {
    KnowledgeGraph kg;
    Fact f;
    f.id = "loc";
    f.type = Fact::Type::location;
    kg.add_or_update_fact(f);

    const Fact* retrieved = kg.fact("loc");
    BOOST_REQUIRE(retrieved != nullptr);
    BOOST_TEST(retrieved->type == Fact::Type::location);
}

BOOST_AUTO_TEST_CASE(nonexistent_fact_returns_null) {
    KnowledgeGraph kg;
    BOOST_TEST(kg.fact("nonexistent") == nullptr);
    BOOST_TEST(kg.fact_mutable("nonexistent") == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
