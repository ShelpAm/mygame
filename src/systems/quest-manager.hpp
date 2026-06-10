#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct QuestObjective {
    std::string type;  // "reach", "talk", "kill"
    std::string target;
    int count = 1;
    int progress = 0;
    std::string text;
};

struct Quest {
    std::string id;
    std::string title_key;
    std::string desc_key;
    std::string giver;
    std::vector<QuestObjective> objectives;
    int reward_trust = 0;
    int reward_gold = 0;
    bool completed = false;
    bool active = false;
};

class QuestManager {
public:
    bool load_from_json(const std::string& json_path);

    bool accept_quest(const std::string& quest_id);
    bool complete_quest(const std::string& quest_id);
    void abandon_quest(const std::string& quest_id);
    const Quest* find_quest(const std::string& quest_id) const;

    void report_kill(const std::string& target, int count = 1);
    void report_talk(const std::string& npc_id);
    void report_reach(const std::string& location_id);
    void report_collect(const std::string& item_id, int count = 1);
    void report_timer(int days_passed = 1);

    const std::vector<Quest>& all_quests() const { return quests_; }
    std::vector<const Quest*> active_quests() const;
    std::vector<const Quest*> available_quests() const;

private:
    std::vector<Quest> quests_;
    int enemies_killed_ = 0;
};
