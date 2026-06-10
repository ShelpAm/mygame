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
    std::string titleKey;
    std::string descKey;
    std::string giver;
    std::vector<QuestObjective> objectives;
    int rewardTrust = 0;
    int rewardGold = 0;
    bool completed = false;
    bool active = false;
};

class QuestManager {
public:
    bool loadFromJson(const std::string& jsonPath);

    bool acceptQuest(const std::string& questId);
    bool completeQuest(const std::string& questId);
    void abandonQuest(const std::string& questId);
    const Quest* findQuest(const std::string& questId) const;

    void reportKill(const std::string& target, int count = 1);
    void reportTalk(const std::string& npcId);
    void reportReach(const std::string& locationId);
    void reportCollect(const std::string& itemId, int count = 1);
    void reportTimer(int daysPassed = 1);

    const std::vector<Quest>& allQuests() const { return m_quests; }
    std::vector<const Quest*> activeQuests() const;
    std::vector<const Quest*> availableQuests() const;

private:
    std::vector<Quest> m_quests;
    int m_enemiesKilled = 0;
};
