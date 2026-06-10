#include "dialogue/relationship-table.hpp"
#include <algorithm>

void RelationshipTable::set_relation(std::string const &npc_id,
                                     Relation const &rel)
{
    relations_[npc_id] = rel;
}

RelationshipTable::Relation const *
RelationshipTable::get_relation(std::string const &npc_id) const
{
    auto it = relations_.find(npc_id);
    return it != relations_.end() ? &it->second : nullptr;
}

RelationshipTable::Relation *
RelationshipTable::get_mutable(std::string const &npc_id)
{
    auto it = relations_.find(npc_id);
    return it != relations_.end() ? &it->second : nullptr;
}

void RelationshipTable::modify_trust(std::string const &npc_id, int delta)
{
    auto &r = relations_[npc_id];
    r.trust = std::clamp(r.trust + delta, -100, 100);
}

void RelationshipTable::modify_fear(std::string const &npc_id, int delta)
{
    auto &r = relations_[npc_id];
    r.fear = std::clamp(r.fear + delta, 0, 100);
}

void RelationshipTable::modify_respect(std::string const &npc_id, int delta)
{
    auto &r = relations_[npc_id];
    r.respect = std::clamp(r.respect + delta, 0, 100);
}

std::vector<std::string> RelationshipTable::all_npc_ids() const
{
    std::vector<std::string> result;
    for (auto const &[id, rel] : relations_) {
        result.push_back(id);
    }
    return result;
}
