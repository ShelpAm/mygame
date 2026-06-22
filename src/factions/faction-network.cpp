#include "factions/faction-network.hpp"
#include <algorithm>

void FactionNetwork::add_faction(Faction faction)
{
    factions_[faction.id] = std::move(faction);
}

Faction *FactionNetwork::get_faction(std::string const &id)
{
    auto it = factions_.find(id);
    return it != factions_.end() ? &it->second : nullptr;
}

Faction const *FactionNetwork::get_faction(std::string const &id) const
{
    auto it = factions_.find(id);
    return it != factions_.end() ? &it->second : nullptr;
}

void FactionNetwork::set_relation(std::string const &a, std::string const &b, int value)
{
    value = std::clamp(value, -100, 100);
    factions_[a].relations[b] = value;
    factions_[b].relations[a] = value;
}

int FactionNetwork::get_relation(std::string const &a, std::string const &b) const
{
    auto itA = factions_.find(a);
    if (itA == factions_.end())
        return 0;
    auto it = itA->second.relations.find(b);
    return it != itA->second.relations.end() ? it->second : 0;
}

void FactionNetwork::modify_power(std::string const &id, int delta)
{
    auto it = factions_.find(id);
    if (it != factions_.end())
        it->second.power = std::clamp(it->second.power + delta, 0, 100);
}

void FactionNetwork::modify_cohesion(std::string const &id, int delta)
{
    auto it = factions_.find(id);
    if (it != factions_.end())
        it->second.cohesion = std::clamp(it->second.cohesion + delta, 0, 100);
}

void FactionNetwork::modify_wealth(std::string const &id, int delta)
{
    auto it = factions_.find(id);
    if (it != factions_.end())
        it->second.wealth = std::clamp(it->second.wealth + delta, 0, 100);
}

std::vector<std::string> FactionNetwork::all_faction_ids() const
{
    std::vector<std::string> result;
    for (auto const &[id, faction] : factions_) {
        result.push_back(id);
    }
    return result;
}

void FactionNetwork::apply_event(std::string const &source_faction, int power_shift,
                                 std::string const &target_faction)
{
    modify_power(source_faction, power_shift);
    if (!target_faction.empty()) {
        modify_power(target_faction, -power_shift / 2);
        // Allies of target react
        auto *target = get_faction(target_faction);
        if (target) {
            for (auto const &[allyId, rel] : target->relations) {
                if (rel > 50 && allyId != source_faction) {
                    modify_power(allyId, -power_shift / 4);
                    set_relation(source_faction, allyId, get_relation(source_faction, allyId) - 10);
                }
            }
        }
    }
}
