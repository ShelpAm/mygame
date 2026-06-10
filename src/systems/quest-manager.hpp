#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct QuestObjective {
    std::string type; // "reach", "talk", "kill"
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
    bool load_from_json(std::string const &json_path);

    bool accept_quest(std::string const &quest_id);
    bool complete_quest(std::string const &quest_id);
    void abandon_quest(std::string const &quest_id);
    Quest const *find_quest(std::string const &quest_id) const;

    void report_kill(std::string const &target, int count = 1);
    void report_talk(std::string const &npc_id);
    void report_reach(std::string const &location_id);
    void report_collect(std::string const &item_id, int count = 1);
    void report_timer(int days_passed = 1);

    std::vector<Quest> const &all_quests() const
    {
        return quests_;
    }
    std::vector<Quest const *> active_quests() const;
    std::vector<Quest const *> available_quests() const;

  private:
    std::vector<Quest> quests_;
    int enemies_killed_ = 0;
};
