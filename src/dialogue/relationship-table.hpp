#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class RelationshipTable {
  public:
    struct Relation {
        int trust = 0;   // -100 to 100
        int fear = 0;    // 0 to 100
        int respect = 0; // 0 to 100
    };

    void set_relation(std::string const &npc_id, Relation const &rel);
    Relation const *get_relation(std::string const &npc_id) const;
    Relation *get_mutable(std::string const &npc_id);

    void modify_trust(std::string const &npc_id, int delta);
    void modify_fear(std::string const &npc_id, int delta);
    void modify_respect(std::string const &npc_id, int delta);

    std::vector<std::string> all_npc_ids() const;

  private:
    std::unordered_map<std::string, Relation> relations_;
};
