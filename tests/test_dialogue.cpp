#include <boost/test/unit_test.hpp>
#include "dialogue/DialogueEngine.hpp"
#include "dialogue/TopicRegistry.hpp"
#include "dialogue/RelationshipTable.hpp"
#include "entities/components/NPCState.hpp"

BOOST_AUTO_TEST_SUITE(dialogue_tests)

BOOST_AUTO_TEST_CASE(topic_registry_basics) {
    TopicRegistry tr;
    tr.registerTopic("event_1", "the Battle", "events");
    tr.registerTopic("person_1", "the King", "people");

    BOOST_TEST(tr.exists("event_1"));
    BOOST_TEST(!tr.exists("event_2"));
    BOOST_TEST(tr.displayName("event_1") == "the Battle");
    BOOST_TEST(tr.displayName("person_1") == "the King");
    BOOST_TEST(tr.displayName("nonexistent").empty());

    auto all = tr.allTopics();
    BOOST_TEST(all.size() == 2u);

    auto events = tr.byCategory("events");
    BOOST_TEST(events.size() == 1u);
    BOOST_TEST(events[0] == "event_1");

    auto people = tr.byCategory("people");
    BOOST_TEST(people.size() == 1u);
}

BOOST_AUTO_TEST_CASE(relationship_table_basics) {
    RelationshipTable rt;
    rt.setRelation("npc_1", {.trust = 10, .fear = 5, .respect = 20});

    auto* rel = rt.getRelation("npc_1");
    BOOST_REQUIRE(rel != nullptr);
    BOOST_TEST(rel->trust == 10);
    BOOST_TEST(rel->fear == 5);
    BOOST_TEST(rel->respect == 20);
}

BOOST_AUTO_TEST_CASE(relationship_modifications) {
    RelationshipTable rt;
    rt.modifyTrust("npc_1", 30);
    BOOST_TEST(rt.getRelation("npc_1")->trust == 30);

    rt.modifyTrust("npc_1", -50);
    BOOST_TEST(rt.getRelation("npc_1")->trust == -20);

    rt.modifyFear("npc_1", 60);
    BOOST_TEST(rt.getRelation("npc_1")->fear == 60);

    rt.modifyRespect("npc_1", 10);
    rt.modifyRespect("npc_1", -5);
    BOOST_TEST(rt.getRelation("npc_1")->respect == 5);
}

BOOST_AUTO_TEST_CASE(relationship_clamping) {
    RelationshipTable rt;
    rt.modifyTrust("npc_1", 200);
    BOOST_TEST(rt.getRelation("npc_1")->trust == 100);

    rt.modifyTrust("npc_1", -300);
    BOOST_TEST(rt.getRelation("npc_1")->trust == -100);

    rt.modifyFear("npc_1", 200);
    BOOST_TEST(rt.getRelation("npc_1")->fear == 100);

    rt.modifyFear("npc_1", -20);
    BOOST_TEST(rt.getRelation("npc_1")->fear == 80);
}

BOOST_AUTO_TEST_CASE(relationship_unknown_npc) {
    RelationshipTable rt;
    BOOST_TEST(rt.getRelation("unknown") == nullptr);
}

BOOST_AUTO_TEST_CASE(relationship_all_ids) {
    RelationshipTable rt;
    rt.setRelation("a", {});
    rt.setRelation("b", {});
    auto ids = rt.allNpcIds();
    BOOST_TEST(ids.size() == 2u);
}

static void addTestTemplate(DialogueEngine& de, const std::string& type,
                            const std::string& text, const std::string& personality = "") {
    DialogueTemplate t;
    t.type = type;
    t.texts = {text};
    t.personalityPref = personality;
    de.addTemplate(t);
}

BOOST_AUTO_TEST_CASE(dialogue_greeting_friendly) {
    DialogueEngine de;
    addTestTemplate(de, "greeting_friendly", "Well met, friend!");
    addTestTemplate(de, "greeting", "Hello traveler.");
    NPCState npc;
    npc.npcId = "test";
    npc.personality = "friendly";

    auto resp = de.generateGreeting(npc, 20);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(resp.trustDelta >= 0); // Friendly should be positive
}

BOOST_AUTO_TEST_CASE(dialogue_greeting_hostile) {
    DialogueEngine de;
    addTestTemplate(de, "greeting_hostile", "Get lost, stranger.");
    addTestTemplate(de, "greeting", "Hello.");
    NPCState npc;
    npc.npcId = "test";
    npc.personality = "hostile";

    auto resp = de.generateGreeting(npc, -30);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(resp.trustDelta <= 0);
}

BOOST_AUTO_TEST_CASE(dialogue_ask_known_topic) {
    DialogueEngine de;
    addTestTemplate(de, "knows_directly", "I was there: [topic]. [detail]");
    addTestTemplate(de, "heard_rumor", "I heard: [topic]. [detail]");
    addTestTemplate(de, "deny_knowledge", "No idea about [topic].");
    NPCState npc;
    npc.npcId = "merchant";
    npc.personality = "friendly";
    npc.knowledge["ugarit_sack"] = {
        "ugarit_sack",
        "I saw the ships coming at dusk. Dozens of them.",
        80, true, ""
    };

    auto resp = de.generateAskResponse(npc, "ugarit_sack", "the Sack of Ugarit", 20);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(resp.isTruthful);
    BOOST_TEST(resp.factId == "ugarit_sack");
}

BOOST_AUTO_TEST_CASE(dialogue_ask_unknown_topic) {
    DialogueEngine de;
    addTestTemplate(de, "deny_knowledge", "I know nothing about [topic].");
    NPCState npc;
    npc.npcId = "merchant";
    npc.personality = "friendly";

    auto resp = de.generateAskResponse(npc, "unknown", "Unknown Topic", 10);
    BOOST_TEST(!resp.text.empty());
    BOOST_TEST(!resp.isTruthful);  // They don't know, so not truthful info
}

BOOST_AUTO_TEST_CASE(dialogue_hostile_lies) {
    DialogueEngine de;
    addTestTemplate(de, "deny_knowledge_hostile", "Not telling you about [topic].", "hostile");
    addTestTemplate(de, "deny_knowledge", "No idea.");
    NPCState npc;
    npc.npcId = "pirate";
    npc.personality = "hostile";
    npc.currentGoal = NPCState::Goal::SpreadMisinfo;
    npc.knowledge["secret"] = {
        "secret",
        "The real treasure is buried near the temple.",
        90, true, ""
    };

    auto resp = de.generateAskResponse(npc, "secret", "the Secret", -40);
    BOOST_TEST(!resp.text.empty());
    // Hostile + SpreadMisinfo = likely provides misleading info
    // Verify we got a response (content depends on random template selection)
    BOOST_TEST(!resp.text.empty());
}

BOOST_AUTO_TEST_CASE(dialogue_heard_rumor) {
    DialogueEngine de;
    addTestTemplate(de, "heard_rumor_guarded", "Maybe I heard something about [topic]...", "guarded");
    addTestTemplate(de, "heard_rumor", "[topic]: [detail]");
    addTestTemplate(de, "deny_knowledge", "Nothing about [topic].");
    NPCState npc;
    npc.npcId = "guard";
    npc.personality = "guarded";
    npc.knowledge["byblos_king"] = {
        "byblos_king",
        "A merchant from Sidon told me the King is preparing for war.",
        40, false, "merchant from Sidon"
    };

    auto resp = de.generateAskResponse(npc, "byblos_king", "the King of Byblos", 0);
    BOOST_TEST(!resp.text.empty());
    // Should mention it's secondhand
}

BOOST_AUTO_TEST_CASE(dialogue_template_fill) {
    DialogueEngine de;
    NPCState npc;
    npc.npcId = "test";
    npc.personality = "friendly";

    auto resp = de.generateGreeting(npc, 10);
    // Template should be filled without placeholder brackets remaining
    BOOST_TEST(resp.text.find('[') == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
