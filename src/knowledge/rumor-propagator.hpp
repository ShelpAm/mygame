#pragma once

#include "knowledge/knowledge-graph.hpp"
#include "entities/components/npc-state.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>

class RumorPropagator {
public:
    RumorPropagator(KnowledgeGraph& knowledge);

    // Called each game day: spread facts through NPC network
    void update(int current_day,
                std::unordered_map<std::string, NPCState>& all_npcs);

    // When two NPCs interact, they exchange knowledge
    void simulate_exchange(NPCState& giver, NPCState& receiver, int current_day);

    // Check if a fact would plausibly reach an NPC
    bool can_reach(const NPCState& npc, const std::string& fact_id) const;

private:
    KnowledgeGraph& knowledge_;

    float apply_distortion(const std::string& fact_id, int hop_count) const;
};
