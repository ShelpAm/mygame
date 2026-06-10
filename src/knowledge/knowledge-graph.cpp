#include "knowledge/knowledge-graph.hpp"
const Fact* KnowledgeGraph::fact(const std::string& id) const {
    auto it = facts_.find(id);
    return it != facts_.end() ? &it->second : nullptr;
}

Fact* KnowledgeGraph::fact_mutable(const std::string& id) {
    auto it = facts_.find(id);
    return it != facts_.end() ? &it->second : nullptr;
}

void KnowledgeGraph::add_or_update_fact(Fact fact) {
    auto it = facts_.find(fact.id);
    if (it != facts_.end()) {
        // Merge origins
        for (auto& origin : fact.origins) {
            it->second.origins.push_back(std::move(origin));
        }
        // Upgrade certainty if multiple sources
        if (it->second.origins.size() >= 3) {
            it->second.player_certainty = Fact::Certainty::plausible;
        }
    } else {
        facts_[fact.id] = std::move(fact);
    }
}

bool KnowledgeGraph::has_fact(const std::string& id) const {
    return facts_.contains(id);
}

void KnowledgeGraph::add_relation(const std::string& fact_a, const std::string& fact_b) {
    fact_relations_.emplace(fact_a, fact_b);
    fact_relations_.emplace(fact_b, fact_a);
}

std::vector<std::string> KnowledgeGraph::related_facts(const std::string& id) const {
    std::vector<std::string> result;
    auto range = fact_relations_.equal_range(id);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    return result;
}

bool KnowledgeGraph::is_topic_known(const std::string& topic) const {
    return known_topics_.contains(topic);
}

void KnowledgeGraph::mark_topic_known(const std::string& topic) {
    known_topics_.insert(topic);
}

std::vector<const Fact*> KnowledgeGraph::facts_witnessed() const {
    std::vector<const Fact*> result;
    for (const auto& [id, fact] : facts_) {
        for (const auto& origin : fact.origins) {
            if (origin.source == Fact::Origin::Source::player_witness) {
                result.push_back(&fact);
                break;
            }
        }
    }
    return result;
}

std::vector<const Fact*> KnowledgeGraph::facts_heard() const {
    std::vector<const Fact*> result;
    for (const auto& [id, fact] : facts_) {
        bool witnessed = false;
        bool heard = false;
        for (const auto& origin : fact.origins) {
            if (origin.source == Fact::Origin::Source::player_witness) {
                witnessed = true;
                break;
            }
            if (origin.source == Fact::Origin::Source::npc_testimony) {
                heard = true;
            }
        }
        if (heard && !witnessed) {
            result.push_back(&fact);
        }
    }
    return result;
}
