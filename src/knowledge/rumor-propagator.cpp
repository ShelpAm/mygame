#include "knowledge/rumor-propagator.hpp"
#include <algorithm>
#include <random>

RumorPropagator::RumorPropagator(KnowledgeGraph &knowledge) : knowledge_(knowledge)
{
}

void RumorPropagator::update(int current_day, std::unordered_map<std::string, NPCState> &all_npcs)
{
    // Each day, randomly pair NPCs for knowledge exchange
    std::vector<std::string> npcIds;
    for (auto &[id, npc] : all_npcs) {
        npcIds.push_back(id);
    }

    if (npcIds.size() < 2)
        return;

    thread_local std::mt19937 rng{std::random_device{}()};
    int exchanges = std::min(3, static_cast<int>(npcIds.size()) / 2);
    std::uniform_int_distribution<size_t> npc_dist(0, npcIds.size() - 1);
    for (int i = 0; i < exchanges; ++i) {
        size_t a = npc_dist(rng);
        size_t b = npc_dist(rng);
        if (a == b)
            continue;

        simulate_exchange(all_npcs[npcIds[a]], all_npcs[npcIds[b]], current_day);
        simulate_exchange(all_npcs[npcIds[b]], all_npcs[npcIds[a]], current_day);
    }
}

void RumorPropagator::simulate_exchange(NPCState &giver, NPCState &receiver, int current_day)
{
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> trust_dist(0, 99);
    std::uniform_int_distribution<int> hop_dist(0, 2);

    for (auto const &[fact_id, known] : giver.knowledge) {
        // Don't share secrets with low trust
        if (known.confidence < 30 && trust_dist(rng) < 70)
            continue;

        bool alreadyKnown = receiver.knowledge.contains(fact_id);

        if (!alreadyKnown) {
            // Learn with distortion
            float distortion = apply_distortion(fact_id, hop_dist(rng) + 1);

            NPCState::KnownFact newFact;
            newFact.fact_id = fact_id;
            newFact.confidence = std::max(0, known.confidence - static_cast<int>(distortion * 30));
            newFact.witnessed = false;
            newFact.source_npc_id = giver.npc_id;

            if (distortion > 0.5f) {
                // Corrupt the version
                newFact.npc_version = known.npc_version + " ...or so I've heard";
            }
            else {
                newFact.npc_version = known.npc_version;
            }

            receiver.knowledge[fact_id] = newFact;
        }
    }
}

bool RumorPropagator::can_reach(NPCState const &npc, std::string const &fact_id) const
{
    return npc.knowledge.contains(fact_id);
}

float RumorPropagator::apply_distortion(std::string const &fact_id, int hop_count) const
{
    // Distortion increases with each retelling
    float base = 0.1f + (hop_count - 1) * 0.15f;
    return std::min(1.f, base);
}
