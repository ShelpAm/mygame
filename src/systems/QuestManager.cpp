#include "systems/QuestManager.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <iostream>

bool QuestManager::loadFromJson(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) return false;
    try {
        std::string content{std::istreambuf_iterator<char>(file), {}};
        auto json = boost::json::parse(content);
        for (const auto& item : json.as_object().at("quests").as_array()) {
            auto& obj = item.as_object();
            Quest q;
            q.id = std::string(obj.at("id").as_string());
            q.titleKey = std::string(obj.at("title").as_string());
            q.descKey = std::string(obj.at("desc").as_string());
            q.giver = std::string(obj.at("giver").as_string());
            for (const auto& o : obj.at("objectives").as_array()) {
                QuestObjective qo;
                qo.type = std::string(o.as_object().at("type").as_string());
                qo.target = std::string(o.as_object().at("target").as_string());
                if (o.as_object().contains("count"))
                    qo.count = static_cast<int>(o.as_object().at("count").as_int64());
                if (o.as_object().contains("textKey"))
                    qo.text = std::string(o.as_object().at("textKey").as_string());
                else if (o.as_object().contains("text"))
                    qo.text = std::string(o.as_object().at("text").as_string());
                q.objectives.push_back(qo);
            }
            q.rewardTrust = static_cast<int>(obj.at("rewards").as_object().at("trust").as_int64());
            q.rewardGold = static_cast<int>(obj.at("rewards").as_object().at("gold").as_int64());
            m_quests.push_back(std::move(q));
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load quests: " << e.what() << '\n';
        return false;
    }
}

bool QuestManager::acceptQuest(const std::string& questId) {
    for (auto& q : m_quests) {
        if (q.id == questId && !q.active && !q.completed) {
            q.active = true;
            return true;
        }
    }
    return false;
}

bool QuestManager::completeQuest(const std::string& questId) {
    for (auto& q : m_quests) {
        if (q.id == questId && q.active) {
            q.completed = true;
            q.active = false;
            return true;
        }
    }
    return false;
}

const Quest* QuestManager::findQuest(const std::string& questId) const {
    for (const auto& q : m_quests)
        if (q.id == questId) return &q;
    return nullptr;
}

void QuestManager::abandonQuest(const std::string& questId) {
    for (auto& q : m_quests) {
        if (q.id == questId) { q.active = false; return; }
    }
}

void QuestManager::reportKill(const std::string& target, int count) {
    m_enemiesKilled += count;
    for (auto& q : m_quests) {
        if (!q.active) continue;
        for (auto& obj : q.objectives) {
            if (obj.type == "kill" && obj.target == target)
                obj.progress = m_enemiesKilled;
        }
    }
}

void QuestManager::reportTalk(const std::string& npcId) {
    for (auto& q : m_quests) {
        if (!q.active) continue;
        for (auto& obj : q.objectives) {
            if (obj.type == "talk" && obj.target == npcId)
                obj.progress = 1;
        }
    }
}

void QuestManager::reportReach(const std::string& locationId) {
    for (auto& q : m_quests) {
        if (!q.active) continue;
        for (auto& obj : q.objectives) {
            if (obj.type == "reach" && obj.target == locationId)
                obj.progress = 1;
        }
    }
}

void QuestManager::reportCollect(const std::string& itemId, int count) {
    for (auto& q : m_quests) {
        if (!q.active) continue;
        for (auto& obj : q.objectives) {
            if (obj.type == "collect" && obj.target == itemId)
                obj.progress += count;
        }
    }
}

void QuestManager::reportTimer(int daysPassed) {
    for (auto& q : m_quests) {
        if (!q.active) continue;
        for (auto& obj : q.objectives) {
            if (obj.type == "timer")
                obj.progress += daysPassed;
        }
    }
}

std::vector<const Quest*> QuestManager::activeQuests() const {
    std::vector<const Quest*> result;
    for (const auto& q : m_quests)
        if (q.active) result.push_back(&q);
    return result;
}

std::vector<const Quest*> QuestManager::availableQuests() const {
    std::vector<const Quest*> result;
    for (const auto& q : m_quests)
        if (!q.active && !q.completed) result.push_back(&q);
    return result;
}
