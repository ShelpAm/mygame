#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct NPCState;

struct DialogueResponse {
    std::string text;
    std::string factId;
    bool isTruthful = true;
    int trustDelta = 0;
    int fearDelta = 0;
    int respectDelta = 0;
    std::vector<std::string> newTopicsSuggested;
};

struct DialogueTemplate {
    std::string type;
    std::vector<std::string> texts;
    bool requiresWitnessed = false;
    bool requiresHeard = false;
    int minConfidence = 0;
    std::string personalityPref;
};

class DialogueEngine {
public:
    enum class Language { English, Chinese };

    DialogueEngine();

    bool loadTemplates(Language lang, const std::string& jsonPath);
    void setLanguage(Language lang);

    DialogueResponse generateAskResponse(
        const NPCState& npc, const std::string& topicId,
        const std::string& topicDisplayName, int playerTrust);

    DialogueResponse generateGreeting(const NPCState& npc, int playerTrust);
    void addTemplate(const DialogueTemplate& tmpl);

private:
    std::vector<DialogueTemplate> m_templates[2];  // 0=en, 1=zh
    Language m_currentLang = Language::English;

    const std::vector<DialogueTemplate>& activeTemplates() const;
    const DialogueTemplate* pickTemplate(const NPCState& npc, bool knowsDirectly,
                                          bool knowsIndirectly, int confidence) const;
    std::string fillTemplate(const std::string& pattern,
                              const std::unordered_map<std::string, std::string>& slots) const;
    bool wouldLie(const NPCState& npc, int playerTrust) const;
};
