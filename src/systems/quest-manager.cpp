#include "systems/quest-manager.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

bool QuestManager::load_from_json(std::string const &json_path)
{
    std::ifstream file(json_path);
    if (!file.is_open())
        return false;
    try {
        std::string content{std::istreambuf_iterator<char>(file), {}};
        auto json = boost::json::parse(content);
        for (auto const &item : json.as_object().at("quests").as_array()) {
            auto &obj = item.as_object();
            Quest q;
            q.id = std::string(obj.at("id").as_string());
            q.title_key = std::string(obj.at("title").as_string());
            q.desc_key = std::string(obj.at("desc").as_string());
            q.giver = std::string(obj.at("giver").as_string());
            for (auto const &o : obj.at("objectives").as_array()) {
                QuestObjective qo;
                qo.type = std::string(o.as_object().at("type").as_string());
                qo.target = std::string(o.as_object().at("target").as_string());
                if (o.as_object().contains("count"))
                    qo.count = static_cast<int>(o.as_object().at("count").as_int64());
                if (o.as_object().contains("text_key"))
                    qo.text = std::string(o.as_object().at("text_key").as_string());
                else if (o.as_object().contains("text"))
                    qo.text = std::string(o.as_object().at("text").as_string());
                q.objectives.push_back(qo);
            }
            q.reward_trust = static_cast<int>(obj.at("rewards").as_object().at("trust").as_int64());
            q.reward_gold = static_cast<int>(obj.at("rewards").as_object().at("gold").as_int64());
            quests_.push_back(std::move(q));
        }
        return true;
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to load quests: {}", e.what());
        return false;
    }
}

bool QuestManager::accept_quest(std::string const &quest_id)
{
    for (auto &q : quests_) {
        if (q.id == quest_id && !q.active && !q.completed) {
            q.active = true;
            return true;
        }
    }
    return false;
}

bool QuestManager::complete_quest(std::string const &quest_id)
{
    for (auto &q : quests_) {
        if (q.id == quest_id && q.active) {
            q.completed = true;
            q.active = false;
            return true;
        }
    }
    return false;
}

Quest const *QuestManager::find_quest(std::string const &quest_id) const
{
    for (auto const &q : quests_)
        if (q.id == quest_id)
            return &q;
    return nullptr;
}

void QuestManager::abandon_quest(std::string const &quest_id)
{
    for (auto &q : quests_) {
        if (q.id == quest_id) {
            q.active = false;
            return;
        }
    }
}

void QuestManager::report_kill(std::string const &target, int count)
{
    enemies_killed_ += count;
    for (auto &q : quests_) {
        if (!q.active)
            continue;
        for (auto &obj : q.objectives) {
            if (obj.type == "kill" && obj.target == target)
                obj.progress = enemies_killed_;
        }
    }
}

void QuestManager::report_talk(std::string const &npc_id)
{
    for (auto &q : quests_) {
        if (!q.active)
            continue;
        for (auto &obj : q.objectives) {
            if (obj.type == "talk" && obj.target == npc_id)
                obj.progress = 1;
        }
    }
}

void QuestManager::report_reach(std::string const &location_id)
{
    for (auto &q : quests_) {
        if (!q.active)
            continue;
        for (auto &obj : q.objectives) {
            if (obj.type == "reach" && obj.target == location_id)
                obj.progress = 1;
        }
    }
}

void QuestManager::report_collect(std::string const &item_id, int count)
{
    for (auto &q : quests_) {
        if (!q.active)
            continue;
        for (auto &obj : q.objectives) {
            if (obj.type == "collect" && obj.target == item_id)
                obj.progress += count;
        }
    }
}

void QuestManager::report_timer(int days_passed)
{
    for (auto &q : quests_) {
        if (!q.active)
            continue;
        for (auto &obj : q.objectives) {
            if (obj.type == "timer")
                obj.progress += days_passed;
        }
    }
}

std::vector<Quest const *> QuestManager::active_quests() const
{
    std::vector<Quest const *> result;
    for (auto const &q : quests_)
        if (q.active)
            result.push_back(&q);
    return result;
}

std::vector<Quest const *> QuestManager::available_quests() const
{
    std::vector<Quest const *> result;
    for (auto const &q : quests_)
        if (!q.active && !q.completed)
            result.push_back(&q);
    return result;
}
