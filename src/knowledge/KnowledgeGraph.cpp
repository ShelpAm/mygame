#include "knowledge/KnowledgeGraph.hpp"

const Fact* KnowledgeGraph::fact(const std::string& id) const {
    auto it = m_facts.find(id);
    return it != m_facts.end() ? &it->second : nullptr;
}

Fact* KnowledgeGraph::factMutable(const std::string& id) {
    auto it = m_facts.find(id);
    return it != m_facts.end() ? &it->second : nullptr;
}

void KnowledgeGraph::addOrUpdateFact(Fact fact) {
    auto it = m_facts.find(fact.id);
    if (it != m_facts.end()) {
        // Merge origins
        for (auto& origin : fact.origins) {
            it->second.origins.push_back(std::move(origin));
        }
        // Upgrade certainty if multiple sources
        if (it->second.origins.size() >= 3) {
            it->second.playerCertainty = Fact::Certainty::Plausible;
        }
    } else {
        m_facts[fact.id] = std::move(fact);
    }
}

bool KnowledgeGraph::hasFact(const std::string& id) const {
    return m_facts.contains(id);
}

void KnowledgeGraph::addRelation(const std::string& factA, const std::string& factB) {
    m_factRelations.emplace(factA, factB);
    m_factRelations.emplace(factB, factA);
}

std::vector<std::string> KnowledgeGraph::relatedFacts(const std::string& id) const {
    std::vector<std::string> result;
    auto range = m_factRelations.equal_range(id);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    return result;
}

bool KnowledgeGraph::isTopicKnown(const std::string& topic) const {
    return m_knownTopics.contains(topic);
}

void KnowledgeGraph::markTopicKnown(const std::string& topic) {
    m_knownTopics.insert(topic);
}

std::vector<const Fact*> KnowledgeGraph::factsWitnessed() const {
    std::vector<const Fact*> result;
    for (const auto& [id, fact] : m_facts) {
        for (const auto& origin : fact.origins) {
            if (origin.source == Fact::Origin::Source::PlayerWitness) {
                result.push_back(&fact);
                break;
            }
        }
    }
    return result;
}

std::vector<const Fact*> KnowledgeGraph::factsHeard() const {
    std::vector<const Fact*> result;
    for (const auto& [id, fact] : m_facts) {
        bool witnessed = false;
        bool heard = false;
        for (const auto& origin : fact.origins) {
            if (origin.source == Fact::Origin::Source::PlayerWitness) {
                witnessed = true;
                break;
            }
            if (origin.source == Fact::Origin::Source::NPCTestimony) {
                heard = true;
            }
        }
        if (heard && !witnessed) {
            result.push_back(&fact);
        }
    }
    return result;
}
