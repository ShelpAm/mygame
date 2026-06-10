#include "dialogue/DialogueEngine.hpp"
#include "entities/components/NPCState.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <cstdlib>
#include <iostream>

DialogueEngine::DialogueEngine() {}

const std::vector<DialogueTemplate>& DialogueEngine::activeTemplates() const {
    return m_templates[static_cast<int>(m_currentLang)];
}

void DialogueEngine::setLanguage(Language lang) {
    m_currentLang = lang;
}

bool DialogueEngine::loadTemplates(Language lang, const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open dialogue templates: " << jsonPath << '\n';
        return false;
    }

    try {
        std::string content{std::istreambuf_iterator<char>(file),
                            std::istreambuf_iterator<char>()};
        auto parsed = boost::json::parse(content);
        auto& arr = parsed.as_object().at("templates").as_array();

        auto& tmpls = m_templates[static_cast<int>(lang)];
        tmpls.clear();

        for (const auto& item : arr) {
            auto& obj = item.as_object();
            DialogueTemplate tmpl;
            tmpl.type = std::string(obj.at("type").as_string());

            for (const auto& t : obj.at("texts").as_array()) {
                tmpl.texts.push_back(std::string(t.as_string()));
            }

            if (obj.contains("requiresWitnessed"))
                tmpl.requiresWitnessed = obj.at("requiresWitnessed").as_bool();
            if (obj.contains("requiresHeard"))
                tmpl.requiresHeard = obj.at("requiresHeard").as_bool();
            if (obj.contains("minConfidence"))
                tmpl.minConfidence = static_cast<int>(obj.at("minConfidence").as_int64());
            if (obj.contains("personalityPref"))
                tmpl.personalityPref = std::string(obj.at("personalityPref").as_string());

            tmpls.push_back(std::move(tmpl));
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse dialogue templates: " << e.what() << '\n';
        return false;
    }
}

void DialogueEngine::addTemplate(const DialogueTemplate& tmpl) {
    m_templates[static_cast<int>(m_currentLang)].push_back(tmpl);
}

DialogueResponse DialogueEngine::generateGreeting(const NPCState& npc, int playerTrust) {
    DialogueResponse resp;

    // First try personality-specific greeting
    for (const auto& t : activeTemplates()) {
        if (t.type == "greeting_" + npc.personality) {
            resp.text = t.texts[std::rand() % t.texts.size()];
            break;
        }
    }

    // Fallback to generic greeting
    if (resp.text.empty()) {
        for (const auto& t : activeTemplates()) {
            if (t.type == "greeting") {
                resp.text = t.texts[std::rand() % t.texts.size()];
                break;
            }
        }
    }

    resp.trustDelta = (npc.personality == "hostile" || npc.personality == "fearful") ? -2 : 2;
    return resp;
}

DialogueResponse DialogueEngine::generateAskResponse(
    const NPCState& npc,
    const std::string& topicId,
    const std::string& topicDisplayName,
    int playerTrust)
{
    DialogueResponse resp;

    auto it = npc.knowledge.find(topicId);
    bool knows = it != npc.knowledge.end();
    bool witnessed = knows && it->second.witnessed;
    bool heardOf = knows && !it->second.witnessed;
    int confidence = knows ? it->second.confidence : 0;
    bool lie = wouldLie(npc, playerTrust);

    const DialogueTemplate* tmpl = nullptr;

    if (lie || !knows || confidence < 10) {
        // Deny or hostile response
        for (const auto& t : activeTemplates()) {
            if (lie && t.type == "deny_knowledge_hostile") { tmpl = &t; break; }
            if (!lie && t.personalityPref == npc.personality && t.type.find("deny") != std::string::npos) { tmpl = &t; break; }
        }
        if (!tmpl) {
            for (const auto& t : activeTemplates()) {
                if (t.type == "deny_knowledge") { tmpl = &t; break; }
            }
        }
    } else {
        tmpl = pickTemplate(npc, witnessed, heardOf, confidence);
    }

    if (!tmpl) {
        for (const auto& t : activeTemplates()) {
            if (t.type == "deny_knowledge") { tmpl = &t; break; }
        }
    }

    std::unordered_map<std::string, std::string> slots;
    slots["topic"] = topicDisplayName;
    if (knows && !lie) {
        slots["detail"] = it->second.npcVersion;
        slots["person"] = it->second.sourceNpcId.empty() ? "a traveler" : it->second.sourceNpcId;
        resp.factId = topicId;
    } else {
        slots["detail"] = "";
        slots["person"] = "someone";
    }

    resp.text = fillTemplate(tmpl->texts[std::rand() % tmpl->texts.size()], slots);
    resp.isTruthful = !lie && knows;

    if (npc.currentGoal == NPCState::Goal::GainInfo && topicId == npc.goalFactId) {
        resp.trustDelta = 5;
        resp.respectDelta = 3;
    } else if (npc.personality == "friendly") {
        resp.trustDelta = 3;
    } else if (npc.personality == "hostile") {
        resp.trustDelta = -3;
    } else {
        resp.trustDelta = 1;
    }

    return resp;
}

const DialogueTemplate* DialogueEngine::pickTemplate(
    const NPCState& npc, bool knowsDirectly, bool knowsIndirectly, int confidence) const
{
    for (const auto& t : activeTemplates()) {
        if (t.type == "greeting") continue;
        if (!t.personalityPref.empty() && t.personalityPref != npc.personality) continue;
        if (t.requiresWitnessed && !knowsDirectly) continue;
        if (t.requiresHeard && !knowsIndirectly) continue;
        if (confidence < t.minConfidence) continue;
        return &t;
    }
    return nullptr;
}

std::string DialogueEngine::fillTemplate(
    const std::string& pattern,
    const std::unordered_map<std::string, std::string>& slots) const
{
    std::string result = pattern;
    for (const auto& [key, value] : slots) {
        std::string placeholder = "[" + key + "]";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

bool DialogueEngine::wouldLie(const NPCState& npc, int playerTrust) const {
    if (npc.currentGoal == NPCState::Goal::SpreadMisinfo) return true;
    if (npc.personality == "hostile" && playerTrust < -20) return true;
    if (npc.currentGoal == NPCState::Goal::HarmPlayer && playerTrust < 0) return true;
    if (npc.personality == "guarded" && npc.urgency > 50 && std::rand() % 100 < 40) return true;
    return false;
}
