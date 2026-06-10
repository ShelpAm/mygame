#include "dialogue/RelationshipTable.hpp"
#include <algorithm>

void RelationshipTable::setRelation(const std::string& npcId, const Relation& rel) {
    m_relations[npcId] = rel;
}

const RelationshipTable::Relation* RelationshipTable::getRelation(const std::string& npcId) const {
    auto it = m_relations.find(npcId);
    return it != m_relations.end() ? &it->second : nullptr;
}

RelationshipTable::Relation* RelationshipTable::getMutable(const std::string& npcId) {
    auto it = m_relations.find(npcId);
    return it != m_relations.end() ? &it->second : nullptr;
}

void RelationshipTable::modifyTrust(const std::string& npcId, int delta) {
    auto& r = m_relations[npcId];
    r.trust = std::clamp(r.trust + delta, -100, 100);
}

void RelationshipTable::modifyFear(const std::string& npcId, int delta) {
    auto& r = m_relations[npcId];
    r.fear = std::clamp(r.fear + delta, 0, 100);
}

void RelationshipTable::modifyRespect(const std::string& npcId, int delta) {
    auto& r = m_relations[npcId];
    r.respect = std::clamp(r.respect + delta, 0, 100);
}

std::vector<std::string> RelationshipTable::allNpcIds() const {
    std::vector<std::string> result;
    for (const auto& [id, rel] : m_relations) {
        result.push_back(id);
    }
    return result;
}
