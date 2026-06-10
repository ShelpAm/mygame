#include "factions/faction-network.hpp"
#include <algorithm>

void FactionNetwork::add_faction(Faction faction) {
    factions_[faction.id] = std::move(faction);
}

Faction* FactionNetwork::get_faction(const std::string& id) {
    auto it = factions_.find(id);
    return it != factions_.end() ? &it->second : nullptr;
}

const Faction* FactionNetwork::get_faction(const std::string& id) const {
    auto it = factions_.find(id);
    return it != factions_.end() ? &it->second : nullptr;
}

void FactionNetwork::set_relation(const std::string& a, const std::string& b, int value) {
    value = std::clamp(value, -100, 100);
    factions_[a].relations[b] = value;
    factions_[b].relations[a] = value;
}

int FactionNetwork::get_relation(const std::string& a, const std::string& b) const {
    auto itA = factions_.find(a);
    if (itA == factions_.end()) return 0;
    auto it = itA->second.relations.find(b);
    return it != itA->second.relations.end() ? it->second : 0;
}

void FactionNetwork::modify_power(const std::string& id, int delta) {
    auto it = factions_.find(id);
    if (it != factions_.end())
        it->second.power = std::clamp(it->second.power + delta, 0, 100);
}

void FactionNetwork::modify_cohesion(const std::string& id, int delta) {
    auto it = factions_.find(id);
    if (it != factions_.end())
        it->second.cohesion = std::clamp(it->second.cohesion + delta, 0, 100);
}

void FactionNetwork::modify_wealth(const std::string& id, int delta) {
    auto it = factions_.find(id);
    if (it != factions_.end())
        it->second.wealth = std::clamp(it->second.wealth + delta, 0, 100);
}

std::vector<std::string> FactionNetwork::all_faction_ids() const {
    std::vector<std::string> result;
    for (const auto& [id, faction] : factions_) {
        result.push_back(id);
    }
    return result;
}

void FactionNetwork::apply_event(const std::string& source_faction, int power_shift,
                                 const std::string& target_faction) {
    modify_power(source_faction, power_shift);
    if (!target_faction.empty()) {
        modify_power(target_faction, -power_shift / 2);
        // Allies of target react
        auto* target = get_faction(target_faction);
        if (target) {
            for (const auto& [allyId, rel] : target->relations) {
                if (rel > 50 && allyId != source_faction) {
                    modify_power(allyId, -power_shift / 4);
                    set_relation(source_faction, allyId,
                                get_relation(source_faction, allyId) - 10);
                }
            }
        }
    }
}
