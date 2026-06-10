#pragma once

#include "knowledge/KnowledgeGraph.hpp"
#include "entities/components/NPCState.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>

class RumorPropagator {
public:
    RumorPropagator(KnowledgeGraph& knowledge);

    // Called each game day: spread facts through NPC network
    void update(int currentDay,
                std::unordered_map<std::string, NPCState>& allNpcs);

    // When two NPCs interact, they exchange knowledge
    void simulateExchange(NPCState& giver, NPCState& receiver, int currentDay);

    // Check if a fact would plausibly reach an NPC
    bool canReach(const NPCState& npc, const std::string& factId) const;

private:
    KnowledgeGraph& m_knowledge;

    float applyDistortion(const std::string& factId, int hopCount) const;
};
