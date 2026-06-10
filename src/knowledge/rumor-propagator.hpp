#pragma once

#include "entities/components/npc-state.hpp"
#include "knowledge/knowledge-graph.hpp"
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

class RumorPropagator {
  public:
    RumorPropagator(KnowledgeGraph &knowledge);

    // Called each game day: spread facts through NPC network
    void update(int current_day,
                std::unordered_map<std::string, NPCState> &all_npcs);

    // When two NPCs interact, they exchange knowledge
    void simulate_exchange(NPCState &giver, NPCState &receiver,
                           int current_day);

    // Check if a fact would plausibly reach an NPC
    bool can_reach(NPCState const &npc, std::string const &fact_id) const;

  private:
    KnowledgeGraph &knowledge_;

    float apply_distortion(std::string const &fact_id, int hop_count) const;
};
