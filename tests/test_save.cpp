// ═══════════════════════════════════════════════════════════════════════════
// RESERVE — SaveManager 的序列化/反序列化方法本身能工作，
// 但完整的存档/读档功能尚未实现（app.cpp 中禁用、load_world 未做、
// player_explored_tiles 被 TODO 跳过）。待 save 功能完整后再启用。
// ═══════════════════════════════════════════════════════════════════════════
#if 0

#include "dialogue/relationship-table.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "save/save-manager.hpp"
#include "world/world-state.hpp"
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>

BOOST_AUTO_TEST_SUITE(save_tests)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_CASE(save_data_default_values)
{
    SaveManager::SaveData data;
    BOOST_TEST(data.day == 1);
    BOOST_TEST(data.season == 0);
    BOOST_TEST(data.time_of_day == 6.F);
    BOOST_TEST(data.player_hp == 20);
    BOOST_TEST(data.player_max_hp == 20);
    BOOST_TEST(data.player_pos.x == 0.F);
    BOOST_TEST(data.player_pos.y == 0.F);
    BOOST_TEST(data.known_topics.empty());
    BOOST_TEST(data.relations.empty());
    BOOST_TEST(data.npcs.empty());
    BOOST_TEST(data.player_explored_tiles.empty());
}

BOOST_AUTO_TEST_CASE(load_rejects_missing_file)
{
    SaveManager::SaveData data;
    BOOST_TEST(!SaveManager::load("/nonexistent/path/save.json", data));
}

BOOST_AUTO_TEST_CASE(load_rejects_invalid_json)
{
    auto tmp = std::filesystem::temp_directory_path() / "test_save_invalid.json";
    {
        std::ofstream f(tmp);
        f << "not valid json";
    }

    SaveManager::SaveData data;
    BOOST_TEST(!SaveManager::load(tmp.string(), data));

    std::filesystem::remove(tmp);
}

BOOST_AUTO_TEST_CASE(save_and_load_round_trip_minimal)
{
    // Set up minimal real objects
    WorldState ws;
    KnowledgeGraph kg;
    RelationshipTable rt;

    SaveManager::NPCData npc;
    npc.id = "npc_test";
    npc.name = "Test NPC";
    npc.personality = "friendly";
    npc.position = {100.F, 200.F};
    npc.hp = 8;
    npc.max_hp = 10;
    npc.alive = true;
    npc.knowledge["fact_1"] = "v1";

    auto tmp = std::filesystem::temp_directory_path() / "test_save_roundtrip.json";

    bool saved = SaveManager::save(tmp.string(), ws, kg, rt, {50.F, 75.F}, 15, 20, {npc});
    BOOST_REQUIRE(saved);
    BOOST_REQUIRE(std::filesystem::exists(tmp));

    // Load back
    SaveManager::SaveData loaded;
    bool loaded_ok = SaveManager::load(tmp.string(), loaded);
    BOOST_REQUIRE(loaded_ok);

    BOOST_TEST(loaded.player_pos.x == 50.F);
    BOOST_TEST(loaded.player_pos.y == 75.F);
    BOOST_TEST(loaded.player_hp == 15);
    BOOST_TEST(loaded.player_max_hp == 20);
    BOOST_TEST(loaded.day == 1);
    BOOST_TEST(loaded.season == 0);
    BOOST_TEST(loaded.time_of_day == 6.F);

    // NPCs
    BOOST_REQUIRE(loaded.npcs.size() == 1u);
    BOOST_TEST(loaded.npcs[0].id == "npc_test");
    BOOST_TEST(loaded.npcs[0].name == "Test NPC");
    BOOST_TEST(loaded.npcs[0].personality == "friendly");
    BOOST_TEST(loaded.npcs[0].position.x == 100.F);
    BOOST_TEST(loaded.npcs[0].position.y == 200.F);
    BOOST_TEST(loaded.npcs[0].hp == 8);
    BOOST_TEST(loaded.npcs[0].max_hp == 10);
    BOOST_TEST(loaded.npcs[0].alive);
    BOOST_TEST(loaded.npcs[0].knowledge.size() == 1u);
    BOOST_TEST(loaded.npcs[0].knowledge.at("fact_1") == "v1");

    std::filesystem::remove(tmp);
}

BOOST_AUTO_TEST_CASE(save_and_load_with_world_state_and_relationships)
{
    // Customise world state
    WorldState ws;
    ws.set_day(7);
    ws.set_season(2);
    ws.set_time_of_day(14.5F);

    // Add location (exercises the LocationState serialisation path
    // via later expansion — currently not in save, but tests construction)
    ws.add_location("ugarit", {"Ugarit", "coast", true, 7, "", 5000});

    // Add relationship
    RelationshipTable rt;
    rt.set_relation("npc_chief", {.trust = 30, .fear = 10, .respect = 50});

    // Add a known topic to KnowledgeGraph
    KnowledgeGraph kg;
    kg.mark_topic_known("topic_ugarit_sack");

    auto tmp = std::filesystem::temp_directory_path() / "test_save_full.json";

    bool saved = SaveManager::save(tmp.string(), ws, kg, rt, {0.F, 0.F}, 20, 20, {});
    BOOST_REQUIRE(saved);

    SaveManager::SaveData loaded;
    BOOST_REQUIRE(SaveManager::load(tmp.string(), loaded));

    // World state
    BOOST_TEST(loaded.day == 7);
    BOOST_TEST(loaded.season == 2);
    BOOST_TEST(loaded.time_of_day == 14.5F);

    // Known topics
    BOOST_REQUIRE(loaded.known_topics.size() == 1u);
    BOOST_TEST(loaded.known_topics[0] == "topic_ugarit_sack");

    // Relationships
    BOOST_REQUIRE(loaded.relations.size() == 1u);
    BOOST_TEST(loaded.relations[0].first == "npc_chief");
    BOOST_TEST(loaded.relations[0].second[0] == 30); // trust
    BOOST_TEST(loaded.relations[0].second[1] == 10); // fear
    BOOST_TEST(loaded.relations[0].second[2] == 50); // respect

    std::filesystem::remove(tmp);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_SUITE_END()

#endif
