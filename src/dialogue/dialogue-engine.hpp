#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct NPCState;

struct DialogueResponse {
    std::string text;
    std::string fact_id;
    bool is_truthful = true;
    int trust_delta = 0;
    int fear_delta = 0;
    int respect_delta = 0;
};

struct DialogueTemplate {
    std::string type;
    std::vector<std::string> texts;
    bool requires_witnessed = false;
    bool requires_heard = false;
    int min_confidence = 0;
    std::string personality_pref;
};

class DialogueEngine {
public:
    DialogueEngine();

    int discover_languages(const std::string& dir);
    void set_language(int lang_index);
    void add_template(const DialogueTemplate& tmpl);  // For tests/content authoring

    DialogueResponse generate_ask_response(const NPCState& npc, const std::string& topic_id,
                                          const std::string& topic_display_name, int player_trust);
    DialogueResponse generate_greeting(const NPCState& npc, int player_trust);

private:
    std::vector<std::vector<DialogueTemplate>> templates_;
    int current_lang_ = 0;

    const std::vector<DialogueTemplate>& active_templates() const;
    const DialogueTemplate* pick_template(const NPCState& npc, bool knows_directly,
                                          bool knows_indirectly, int confidence) const;
    std::string fill_template(const std::string& pattern,
                              const std::unordered_map<std::string, std::string>& slots) const;
    bool would_lie(const NPCState& npc, int player_trust) const;
};
