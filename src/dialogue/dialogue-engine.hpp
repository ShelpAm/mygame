#pragma once

#include <string>
#include <unordered_map>
#include <vector>

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

    int discover_languages(std::string const &dir);
    void set_language(int lang_index);
    void
    add_template(DialogueTemplate const &tmpl); // For tests/content authoring

    DialogueResponse
    generate_ask_response(NPCState const &npc, std::string const &topic_id,
                          std::string const &topic_display_name,
                          int player_trust);
    DialogueResponse generate_greeting(NPCState const &npc, int player_trust);

  private:
    std::vector<std::vector<DialogueTemplate>> templates_;
    int current_lang_ = 0;

    std::vector<DialogueTemplate> const &active_templates() const;
    DialogueTemplate const *pick_template(NPCState const &npc,
                                          bool knows_directly,
                                          bool knows_indirectly,
                                          int confidence) const;
    std::string fill_template(
        std::string const &pattern,
        std::unordered_map<std::string, std::string> const &slots) const;
    bool would_lie(NPCState const &npc, int player_trust) const;
};
