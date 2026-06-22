#include "dialogue/dialogue-engine.hpp"
#include "entities/components/npc-state.hpp"
#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <spdlog/spdlog.h>

DialogueEngine::DialogueEngine()
{
}

std::vector<DialogueTemplate> const &DialogueEngine::active_templates() const
{
    return templates_[current_lang_];
}

void DialogueEngine::set_language(int lang_index)
{
    if (lang_index >= 0 && lang_index < static_cast<int>(templates_.size()))
        current_lang_ = lang_index;
}

int DialogueEngine::discover_languages(std::string const &dir)
{
    templates_.clear();
    try {
        for (auto const &entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() != ".json")
                continue;

            std::ifstream file(entry.path().string());
            if (!file)
                continue;

            std::string content{std::istreambuf_iterator<char>(file), {}};
            auto parsed = boost::json::parse(content);
            auto &arr = parsed.as_object().at("templates").as_array();

            std::vector<DialogueTemplate> tmpls;
            for (auto const &item : arr) {
                auto &obj = item.as_object();
                DialogueTemplate t;
                t.type = std::string(obj.at("type").as_string());
                for (auto const &txt : obj.at("texts").as_array())
                    t.texts.push_back(std::string(txt.as_string()));
                if (obj.contains("requires_witnessed"))
                    t.requires_witnessed = obj.at("requires_witnessed").as_bool();
                if (obj.contains("requires_heard"))
                    t.requires_heard = obj.at("requires_heard").as_bool();
                if (obj.contains("min_confidence"))
                    t.min_confidence = static_cast<int>(obj.at("min_confidence").as_int64());
                if (obj.contains("personality_pref"))
                    t.personality_pref = std::string(obj.at("personality_pref").as_string());
                tmpls.push_back(std::move(t));
            }
            templates_.push_back(std::move(tmpls));
        }
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to discover dialogue templates: {}", e.what());
    }
    return static_cast<int>(templates_.size());
}

void DialogueEngine::add_template(DialogueTemplate const &tmpl)
{
    while (templates_.empty())
        templates_.emplace_back();
    templates_[current_lang_].push_back(tmpl);
}

DialogueResponse DialogueEngine::generate_greeting(NPCState const &npc, int player_trust)
{
    thread_local std::mt19937 rng{std::random_device{}()};
    DialogueResponse resp;

    // First try personality-specific greeting
    for (auto const &t : active_templates()) {
        if (t.type == "greeting_" + npc.personality) {
            std::uniform_int_distribution<size_t> dist(0, t.texts.size() - 1);
            resp.text = t.texts[dist(rng)];
            break;
        }
    }

    // Fallback to generic greeting
    if (resp.text.empty()) {
        for (auto const &t : active_templates()) {
            if (t.type == "greeting") {
                std::uniform_int_distribution<size_t> dist(0, t.texts.size() - 1);
                resp.text = t.texts[dist(rng)];
                break;
            }
        }
    }

    resp.trust_delta = (npc.personality == "hostile" || npc.personality == "fearful") ? -2 : 2;
    return resp;
}

DialogueResponse DialogueEngine::generate_ask_response(NPCState const &npc,
                                                       std::string const &topic_id,
                                                       std::string const &topic_display_name,
                                                       int player_trust)
{
    DialogueResponse resp;

    auto it = npc.knowledge.find(topic_id);
    bool knows = it != npc.knowledge.end();
    bool witnessed = knows && it->second.witnessed;
    bool heardOf = knows && !it->second.witnessed;
    int confidence = knows ? it->second.confidence : 0;
    bool lie = would_lie(npc, player_trust);

    DialogueTemplate const *tmpl = nullptr;

    if (lie || !knows || confidence < 10) {
        // Deny or hostile response
        for (auto const &t : active_templates()) {
            if (lie && t.type == "deny_knowledge_hostile") {
                tmpl = &t;
                break;
            }
            if (!lie && t.personality_pref == npc.personality &&
                t.type.find("deny") != std::string::npos) {
                tmpl = &t;
                break;
            }
        }
        if (!tmpl) {
            for (auto const &t : active_templates()) {
                if (t.type == "deny_knowledge") {
                    tmpl = &t;
                    break;
                }
            }
        }
    }
    else {
        tmpl = pick_template(npc, witnessed, heardOf, confidence);
    }

    if (!tmpl) {
        for (auto const &t : active_templates()) {
            if (t.type == "deny_knowledge") {
                tmpl = &t;
                break;
            }
        }
    }

    std::unordered_map<std::string, std::string> slots;
    slots["topic"] = topic_display_name;
    if (knows && !lie) {
        slots["detail"] = it->second.npc_version;
        slots["person"] =
            it->second.source_npc_id.empty() ? "a traveler" : it->second.source_npc_id;
        resp.fact_id = topic_id;
    }
    else {
        slots["detail"] = "";
        slots["person"] = "someone";
    }

    {
        thread_local std::mt19937 rng2{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist(0, tmpl->texts.size() - 1);
        resp.text = fill_template(tmpl->texts[dist(rng2)], slots);
    }
    resp.is_truthful = !lie && knows;

    if (npc.current_goal == NPCState::Goal::gain_info && topic_id == npc.goal_fact_id) {
        resp.trust_delta = 5;
        resp.respect_delta = 3;
    }
    else if (npc.personality == "friendly") {
        resp.trust_delta = 3;
    }
    else if (npc.personality == "hostile") {
        resp.trust_delta = -3;
    }
    else {
        resp.trust_delta = 1;
    }

    return resp;
}

DialogueTemplate const *DialogueEngine::pick_template(NPCState const &npc, bool knows_directly,
                                                      bool knows_indirectly, int confidence) const
{
    for (auto const &t : active_templates()) {
        if (t.type == "greeting")
            continue;
        if (!t.personality_pref.empty() && t.personality_pref != npc.personality)
            continue;
        if (t.requires_witnessed && !knows_directly)
            continue;
        if (t.requires_heard && !knows_indirectly)
            continue;
        if (confidence < t.min_confidence)
            continue;
        return &t;
    }
    return nullptr;
}

std::string
DialogueEngine::fill_template(std::string const &pattern,
                              std::unordered_map<std::string, std::string> const &slots) const
{
    std::string result = pattern;
    for (auto const &[key, value] : slots) {
        std::string placeholder = "[" + key + "]";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

bool DialogueEngine::would_lie(NPCState const &npc, int player_trust) const
{
    if (npc.current_goal == NPCState::Goal::spread_misinfo)
        return true;
    if (npc.personality == "hostile" && player_trust < -20)
        return true;
    if (npc.current_goal == NPCState::Goal::harm_player && player_trust < 0)
        return true;
    if (npc.personality == "guarded" && npc.urgency > 50) {
        thread_local std::mt19937 rng3{std::random_device{}()};
        std::uniform_int_distribution<int> dist(0, 99);
        if (dist(rng3) < 40)
            return true;
    }
    return false;
}
