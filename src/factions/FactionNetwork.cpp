#include "factions/FactionNetwork.hpp"
#include <algorithm>

void FactionNetwork::addFaction(Faction faction) {
    m_factions[faction.id] = std::move(faction);
}

Faction* FactionNetwork::getFaction(const std::string& id) {
    auto it = m_factions.find(id);
    return it != m_factions.end() ? &it->second : nullptr;
}

const Faction* FactionNetwork::getFaction(const std::string& id) const {
    auto it = m_factions.find(id);
    return it != m_factions.end() ? &it->second : nullptr;
}

void FactionNetwork::setRelation(const std::string& a, const std::string& b, int value) {
    value = std::clamp(value, -100, 100);
    m_factions[a].relations[b] = value;
    m_factions[b].relations[a] = value;
}

int FactionNetwork::getRelation(const std::string& a, const std::string& b) const {
    auto itA = m_factions.find(a);
    if (itA == m_factions.end()) return 0;
    auto it = itA->second.relations.find(b);
    return it != itA->second.relations.end() ? it->second : 0;
}

void FactionNetwork::modifyPower(const std::string& id, int delta) {
    auto it = m_factions.find(id);
    if (it != m_factions.end())
        it->second.power = std::clamp(it->second.power + delta, 0, 100);
}

void FactionNetwork::modifyCohesion(const std::string& id, int delta) {
    auto it = m_factions.find(id);
    if (it != m_factions.end())
        it->second.cohesion = std::clamp(it->second.cohesion + delta, 0, 100);
}

void FactionNetwork::modifyWealth(const std::string& id, int delta) {
    auto it = m_factions.find(id);
    if (it != m_factions.end())
        it->second.wealth = std::clamp(it->second.wealth + delta, 0, 100);
}

std::vector<std::string> FactionNetwork::allFactionIds() const {
    std::vector<std::string> result;
    for (const auto& [id, faction] : m_factions) {
        result.push_back(id);
    }
    return result;
}

void FactionNetwork::applyEvent(const std::string& sourceFaction, int powerShift,
                                 const std::string& targetFaction) {
    modifyPower(sourceFaction, powerShift);
    if (!targetFaction.empty()) {
        modifyPower(targetFaction, -powerShift / 2);
        // Allies of target react
        auto* target = getFaction(targetFaction);
        if (target) {
            for (const auto& [allyId, rel] : target->relations) {
                if (rel > 50 && allyId != sourceFaction) {
                    modifyPower(allyId, -powerShift / 4);
                    setRelation(sourceFaction, allyId,
                                getRelation(sourceFaction, allyId) - 10);
                }
            }
        }
    }
}
