#include "knowledge/RumorPropagator.hpp"
#include <algorithm>

RumorPropagator::RumorPropagator(KnowledgeGraph& knowledge)
    : m_knowledge(knowledge)
{}

void RumorPropagator::update(int currentDay,
                              std::unordered_map<std::string, NPCState>& allNpcs) {
    // Each day, randomly pair NPCs for knowledge exchange
    std::vector<std::string> npcIds;
    for (auto& [id, npc] : allNpcs) {
        npcIds.push_back(id);
    }

    if (npcIds.size() < 2) return;

    int exchanges = std::min(3, (int)npcIds.size() / 2);
    for (int i = 0; i < exchanges; ++i) {
        int a = std::rand() % npcIds.size();
        int b = std::rand() % npcIds.size();
        if (a == b) continue;

        simulateExchange(allNpcs[npcIds[a]], allNpcs[npcIds[b]], currentDay);
        simulateExchange(allNpcs[npcIds[b]], allNpcs[npcIds[a]], currentDay);
    }
}

void RumorPropagator::simulateExchange(NPCState& giver, NPCState& receiver,
                                        int currentDay) {
    for (const auto& [factId, known] : giver.knowledge) {
        // Don't share secrets with low trust
        if (known.confidence < 30 && std::rand() % 100 < 70) continue;

        bool alreadyKnown = receiver.knowledge.contains(factId);

        if (!alreadyKnown) {
            // Learn with distortion
            float distortion = applyDistortion(factId, std::rand() % 3 + 1);

            NPCState::KnownFact newFact;
            newFact.factId = factId;
            newFact.confidence = std::max(0, known.confidence - (int)(distortion * 30));
            newFact.witnessed = false;
            newFact.sourceNpcId = giver.npcId;

            if (distortion > 0.5f) {
                // Corrupt the version
                newFact.npcVersion = known.npcVersion + " ...or so I've heard";
            } else {
                newFact.npcVersion = known.npcVersion;
            }

            receiver.knowledge[factId] = newFact;
        }
    }
}

bool RumorPropagator::canReach(const NPCState& npc, const std::string& factId) const {
    return npc.knowledge.contains(factId);
}

float RumorPropagator::applyDistortion(const std::string& factId, int hopCount) const {
    // Distortion increases with each retelling
    float base = 0.1f + (hopCount - 1) * 0.15f;
    return std::min(1.f, base);
}
