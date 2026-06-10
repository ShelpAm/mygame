#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class RelationshipTable {
public:
    struct Relation {
        int trust = 0;     // -100 to 100
        int fear = 0;      // 0 to 100
        int respect = 0;   // 0 to 100
    };

    void set_relation(const std::string& npc_id, const Relation& rel);
    const Relation* get_relation(const std::string& npc_id) const;
    Relation* get_mutable(const std::string& npc_id);

    void modify_trust(const std::string& npc_id, int delta);
    void modify_fear(const std::string& npc_id, int delta);
    void modify_respect(const std::string& npc_id, int delta);

    std::vector<std::string> all_npc_ids() const;

private:
    std::unordered_map<std::string, Relation> relations_;
};
