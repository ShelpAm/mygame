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
    DialogueEngine();

    int discoverLanguages(const std::string& dir);
    void setLanguage(int langIndex);
    void addTemplate(const DialogueTemplate& tmpl);  // For tests/content authoring

    DialogueResponse generateAskResponse(const NPCState& npc, const std::string& topicId,
                                          const std::string& topicDisplayName, int playerTrust);
    DialogueResponse generateGreeting(const NPCState& npc, int playerTrust);

private:
    std::vector<std::vector<DialogueTemplate>> m_templates;
    int m_currentLang = 0;

    const std::vector<DialogueTemplate>& activeTemplates() const;
    const DialogueTemplate* pickTemplate(const NPCState& npc, bool knowsDirectly,
                                          bool knowsIndirectly, int confidence) const;
    std::string fillTemplate(const std::string& pattern,
                              const std::unordered_map<std::string, std::string>& slots) const;
    bool wouldLie(const NPCState& npc, int playerTrust) const;
};
