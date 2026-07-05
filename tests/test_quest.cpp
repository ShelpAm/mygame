#include "systems/quest-manager.hpp"
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>

BOOST_AUTO_TEST_SUITE(quest_tests)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_CASE(manager_starts_empty)
{
    QuestManager qm;
    BOOST_TEST(qm.all_quests().empty());
    BOOST_TEST(qm.active_quests().empty());
    BOOST_TEST(qm.available_quests().empty());
    BOOST_TEST(qm.find_quest("nonexistent") == nullptr);
}

BOOST_AUTO_TEST_CASE(unknown_quest_ops_return_false)
{
    QuestManager qm;
    BOOST_TEST(!qm.accept_quest("unknown"));
    BOOST_TEST(!qm.complete_quest("unknown"));
}

BOOST_AUTO_TEST_CASE(report_methods_dont_crash_when_empty)
{
    QuestManager qm;
    qm.report_kill("bandit", 1);
    qm.report_talk("merchant");
    qm.report_reach("town");
    qm.report_collect("wood", 5);
    qm.report_timer(1);
    qm.abandon_quest("nonexistent");
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_CASE(load_from_json_rejects_bad_path)
{
    QuestManager qm;
    BOOST_TEST(!qm.load_from_json("/nonexistent/path/quests.json"));
}

BOOST_AUTO_TEST_CASE(load_from_json_parses_valid_file)
{
    auto tmp = std::filesystem::temp_directory_path() / "test_quests_load.json";
    {
        std::ofstream f(tmp);
        f << R"({
            "quests": [
                {
                    "id": "q_kill",
                    "title": "Kill Quest",
                    "desc": "Go kill things",
                    "giver": "npc_1",
                    "objectives": [
                        {"type": "kill", "target": "bandit", "count": 3, "text_key": "quest.kill_bandits"}
                    ],
                    "rewards": {"trust": 10, "gold": 50}
                },
                {
                    "id": "q_talk",
                    "title": "Talk Quest",
                    "desc": "Talk to someone",
                    "giver": "npc_2",
                    "objectives": [
                        {"type": "talk", "target": "elder", "text": "Talk to the elder"}
                    ],
                    "rewards": {"trust": 5, "gold": 25}
                }
            ]
        })";
    }

    QuestManager qm;
    BOOST_REQUIRE(qm.load_from_json(tmp.string()));
    BOOST_TEST(qm.all_quests().size() == 2u);

    // Find first quest
    auto const *q = qm.find_quest("q_kill");
    BOOST_REQUIRE(q != nullptr);
    BOOST_TEST(q->title_key == "Kill Quest");
    BOOST_TEST(q->giver == "npc_1");
    BOOST_TEST(q->objectives.size() == 1u);
    BOOST_TEST(q->objectives[0].type == "kill");
    BOOST_TEST(q->objectives[0].target == "bandit");
    BOOST_TEST(q->objectives[0].count == 3);
    BOOST_TEST(q->objectives[0].progress == 0);
    BOOST_TEST(q->reward_gold == 50);
    BOOST_TEST(!q->active);
    BOOST_TEST(!q->completed);

    // Available initially
    BOOST_TEST(qm.available_quests().size() == 2u);
    BOOST_TEST(qm.active_quests().empty());

    std::filesystem::remove(tmp);
}

BOOST_AUTO_TEST_CASE(accept_and_complete_quest_flow)
{
    auto tmp = std::filesystem::temp_directory_path() / "test_quests_flow.json";
    {
        std::ofstream f(tmp);
        f << R"({
            "quests": [
                {
                    "id": "q_one",
                    "title": "One",
                    "desc": "First quest",
                    "giver": "npc_1",
                    "objectives": [
                        {"type": "kill", "target": "rat", "count": 5, "text_key": "quest.kill_rats"}
                    ],
                    "rewards": {"trust": 5, "gold": 10}
                },
                {
                    "id": "q_two",
                    "title": "Two",
                    "desc": "Second quest",
                    "giver": "npc_2",
                    "objectives": [],
                    "rewards": {"trust": 0, "gold": 0}
                }
            ]
        })";
    }

    QuestManager qm;
    BOOST_REQUIRE(qm.load_from_json(tmp.string()));

    // Accept q_one
    BOOST_TEST(qm.accept_quest("q_one"));
    BOOST_TEST(!qm.accept_quest("q_one")); // already active
    BOOST_TEST(qm.find_quest("q_one")->active);

    // Check active / available split
    BOOST_TEST(qm.active_quests().size() == 1u);
    BOOST_TEST(qm.available_quests().size() == 1u);

    // Report kills
    qm.report_kill("rat", 5);
    auto const *q = qm.find_quest("q_one");
    BOOST_REQUIRE(q != nullptr);
    BOOST_TEST(q->objectives[0].progress >= 5);

    // Complete
    BOOST_TEST(qm.complete_quest("q_one"));
    BOOST_TEST(qm.find_quest("q_one")->completed);
    BOOST_TEST(!qm.find_quest("q_one")->active);
    BOOST_TEST(qm.active_quests().empty());
    BOOST_TEST(qm.available_quests().size() == 1u); // q_two still available

    // Complete already-completed quest returns false
    BOOST_TEST(!qm.complete_quest("q_one"));

    std::filesystem::remove(tmp);
}

BOOST_AUTO_TEST_CASE(report_methods_update_objectives)
{
    auto tmp = std::filesystem::temp_directory_path() / "test_quests_report.json";
    {
        std::ofstream f(tmp);
        f << R"({
            "quests": [
                {
                    "id": "q_multi",
                    "title": "Multi Objective",
                    "desc": "Do many things",
                    "giver": "npc_1",
                    "objectives": [
                        {"type": "kill", "target": "wolf", "count": 3, "text_key": "quest.kill_wolves"},
                        {"type": "talk", "target": "chief", "text_key": "quest.talk_chief"},
                        {"type": "reach", "target": "village", "text_key": "quest.reach_village"},
                        {"type": "collect", "target": "herb", "count": 5, "text_key": "quest.collect_herbs"},
                        {"type": "timer", "target": "", "count": 7, "text_key": "quest.survive"}
                    ],
                    "rewards": {"trust": 20, "gold": 100}
                }
            ]
        })";
    }

    QuestManager qm;
    BOOST_REQUIRE(qm.load_from_json(tmp.string()));
    BOOST_REQUIRE(qm.accept_quest("q_multi"));

    // Initially all objectives at 0
    auto const *q = qm.find_quest("q_multi");
    BOOST_REQUIRE(q != nullptr);
    for (auto const &obj : q->objectives)
        BOOST_TEST(obj.progress == 0);

    // Report each type
    qm.report_kill("wolf", 2);
    qm.report_kill("wolf", 1); // incremental
    BOOST_TEST(q->objectives[0].progress >= 3);

    qm.report_talk("chief");
    BOOST_TEST(q->objectives[1].progress == 1);

    qm.report_reach("village");
    BOOST_TEST(q->objectives[2].progress == 1);

    qm.report_collect("herb", 5);
    BOOST_TEST(q->objectives[3].progress >= 5);

    qm.report_timer(7);
    BOOST_TEST(q->objectives[4].progress >= 7);

    std::filesystem::remove(tmp);
}

BOOST_AUTO_TEST_CASE(abandon_quest)
{
    auto tmp = std::filesystem::temp_directory_path() / "test_quests_abandon.json";
    {
        std::ofstream f(tmp);
        f << R"({
            "quests": [
                {
                    "id": "q_abandon",
                    "title": "Abandon Me",
                    "desc": "A quest to abandon",
                    "giver": "npc_1",
                    "objectives": [],
                    "rewards": {"trust": 0, "gold": 0}
                }
            ]
        })";
    }

    QuestManager qm;
    BOOST_REQUIRE(qm.load_from_json(tmp.string()));
    BOOST_REQUIRE(qm.accept_quest("q_abandon"));
    BOOST_TEST(qm.active_quests().size() == 1u);

    qm.abandon_quest("q_abandon");
    BOOST_TEST(!qm.find_quest("q_abandon")->active);
    BOOST_TEST(qm.active_quests().empty());

    std::filesystem::remove(tmp);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_SUITE_END()
