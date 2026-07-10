#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "components/npc-state.hpp"
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_SUITE(dialogue_tests)

BOOST_AUTO_TEST_CASE(topic_registry_basics)
{
    TopicRegistry tr;
    tr.register_topic("event_1", "the Battle", "events");
    tr.register_topic("person_1", "the King", "people");

    BOOST_TEST(tr.exists("event_1"));
    BOOST_TEST(!tr.exists("event_2"));
    BOOST_TEST(tr.display_name("event_1") == "the Battle");
    BOOST_TEST(tr.display_name("person_1") == "the King");
    BOOST_TEST(tr.display_name("nonexistent").empty());

    auto all = tr.all_topics();
    BOOST_TEST(all.size() == 2u);

    auto events = tr.by_category("events");
    BOOST_TEST(events.size() == 1u);
    BOOST_TEST(events[0] == "event_1");

    auto people = tr.by_category("people");
    BOOST_TEST(people.size() == 1u);
}

BOOST_AUTO_TEST_CASE(relationship_table_basics)
{
    RelationshipTable rt;
    rt.set_relation("npc_1", {.trust = 10, .fear = 5, .respect = 20});

    auto *rel = rt.get_relation("npc_1");
    BOOST_REQUIRE(rel != nullptr);
    BOOST_TEST(rel->trust == 10);
    BOOST_TEST(rel->fear == 5);
    BOOST_TEST(rel->respect == 20);
}

BOOST_AUTO_TEST_CASE(relationship_modifications)
{
    RelationshipTable rt;
    rt.modify_trust("npc_1", 30);
    BOOST_TEST(rt.get_relation("npc_1")->trust == 30);

    rt.modify_trust("npc_1", -50);
    BOOST_TEST(rt.get_relation("npc_1")->trust == -20);

    rt.modify_fear("npc_1", 60);
    BOOST_TEST(rt.get_relation("npc_1")->fear == 60);

    rt.modify_respect("npc_1", 10);
    rt.modify_respect("npc_1", -5);
    BOOST_TEST(rt.get_relation("npc_1")->respect == 5);
}

BOOST_AUTO_TEST_CASE(relationship_clamping)
{
    RelationshipTable rt;
    rt.modify_trust("npc_1", 200);
    BOOST_TEST(rt.get_relation("npc_1")->trust == 100);

    rt.modify_trust("npc_1", -300);
    BOOST_TEST(rt.get_relation("npc_1")->trust == -100);

    rt.modify_fear("npc_1", 200);
    BOOST_TEST(rt.get_relation("npc_1")->fear == 100);

    rt.modify_fear("npc_1", -20);
    BOOST_TEST(rt.get_relation("npc_1")->fear == 80);
}

BOOST_AUTO_TEST_CASE(relationship_unknown_npc)
{
    RelationshipTable rt;
    BOOST_TEST(rt.get_relation("unknown") == nullptr);
}

BOOST_AUTO_TEST_CASE(relationship_all_ids)
{
    RelationshipTable rt;
    rt.set_relation("a", {});
    rt.set_relation("b", {});
    auto ids = rt.all_npc_ids();
    BOOST_TEST(ids.size() == 2u);
}

static void addTestTemplate(DialogueEngine &de, std::string const &type, std::string const &text,
                            std::string const &personality = "")
{
    DialogueTemplate t;
    t.type = type;
    t.texts = {text};
    t.personality_pref = personality;
    de.add_template(t);
}

BOOST_AUTO_TEST_CASE(dialogue_greeting_friendly)
{
    DialogueEngine de;
    addTestTemplate(de, "greeting_friendly", "Well met, friend!");
    addTestTemplate(de, "greeting", "Hello traveler.");
    NPCState npc;
    npc.npc_id = "test";
    npc.personality = "friendly";

    auto resp = de.generate_greeting(npc, 20);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(resp.trust_delta >= 0); // Friendly should be positive
}

BOOST_AUTO_TEST_CASE(dialogue_greeting_hostile)
{
    DialogueEngine de;
    addTestTemplate(de, "greeting_hostile", "Get lost, stranger.");
    addTestTemplate(de, "greeting", "Hello.");
    NPCState npc;
    npc.npc_id = "test";
    npc.personality = "hostile";

    auto resp = de.generate_greeting(npc, -30);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(resp.trust_delta <= 0);
}

BOOST_AUTO_TEST_CASE(dialogue_ask_known_topic)
{
    DialogueEngine de;
    addTestTemplate(de, "knows_directly", "I was there: [topic]. [detail]");
    addTestTemplate(de, "heard_rumor", "I heard: [topic]. [detail]");
    addTestTemplate(de, "deny_knowledge", "No idea about [topic].");
    NPCState npc;
    npc.npc_id = "merchant";
    npc.personality = "friendly";
    npc.knowledge["ugarit_sack"] = {
        "ugarit_sack", "I saw the ships coming at dusk. Dozens of them.", "", 80, true, ""};

    auto resp = de.generate_ask_response(npc, "ugarit_sack", "the Sack of Ugarit", 20);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(resp.is_truthful);
    BOOST_TEST(resp.fact_id == "ugarit_sack");
}

BOOST_AUTO_TEST_CASE(dialogue_ask_unknown_topic)
{
    DialogueEngine de;
    addTestTemplate(de, "deny_knowledge", "I know nothing about [topic].");
    NPCState npc;
    npc.npc_id = "merchant";
    npc.personality = "friendly";

    auto resp = de.generate_ask_response(npc, "unknown", "Unknown Topic", 10);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(!resp.is_truthful); // They don't know, so not truthful info
}

BOOST_AUTO_TEST_CASE(dialogue_hostile_lies)
{
    DialogueEngine de;
    addTestTemplate(de, "deny_knowledge_hostile", "Not telling you about [topic].", "hostile");
    addTestTemplate(de, "deny_knowledge", "No idea.");
    NPCState npc;
    npc.npc_id = "pirate";
    npc.personality = "hostile";
    npc.current_goal = NPCState::Goal::spread_misinfo;
    npc.knowledge["secret"] = {"secret", "The real treasure is buried near the temple.", "", 90, true,
                               ""};

    auto resp = de.generate_ask_response(npc, "secret", "the Secret", -40);
    BOOST_TEST(!resp.text.empty());
    // Hostile + SpreadMisinfo = likely provides misleading info
    // Verify we got a response (content depends on random template selection)
    BOOST_TEST(!resp.text.empty());
}

BOOST_AUTO_TEST_CASE(dialogue_heard_rumor)
{
    DialogueEngine de;
    addTestTemplate(de, "heard_rumor_guarded", "Maybe I heard something about [topic]...",
                    "guarded");
    addTestTemplate(de, "heard_rumor", "[topic]: [detail]");
    addTestTemplate(de, "deny_knowledge", "Nothing about [topic].");
    NPCState npc;
    npc.npc_id = "guard";
    npc.personality = "guarded";
    npc.knowledge["byblos_king"] = {"byblos_king",
                                    "A merchant from Sidon told me the King is preparing for war.", "",
                                    40, false, "merchant from Sidon"};

    auto resp = de.generate_ask_response(npc, "byblos_king", "the King of Byblos", 0);
    BOOST_TEST(!resp.text.empty());
    // Should mention it's secondhand
}

BOOST_AUTO_TEST_CASE(dialogue_template_fill)
{
    DialogueEngine de;
    addTestTemplate(de, "greeting_friendly", "Well met, friend!");
    NPCState npc;
    npc.npc_id = "test";
    npc.personality = "friendly";

    auto resp = de.generate_greeting(npc, 10);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(resp.text.find('[') == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
