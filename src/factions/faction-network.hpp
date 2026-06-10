#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct Faction {
    std::string id;
    std::string name;
    int power = 50;    // 0-100, military/economic strength
    int cohesion = 50; // 0-100, internal unity
    int wealth = 50;   // 0-100, material resources

    std::vector<std::string> controlled_locations;
    std::vector<std::string> member_npc_ids;
    std::string leader_npc_id;

    // Faction-to-faction relations: -100 (war) to 100 (allied)
    std::unordered_map<std::string, int> relations;
};

class FactionNetwork {
  public:
    void add_faction(Faction faction);
    Faction *get_faction(std::string const &id);
    Faction const *get_faction(std::string const &id) const;

    void set_relation(std::string const &a, std::string const &b, int value);
    int get_relation(std::string const &a, std::string const &b) const;

    void modify_power(std::string const &id, int delta);
    void modify_cohesion(std::string const &id, int delta);
    void modify_wealth(std::string const &id, int delta);

    std::vector<std::string> all_faction_ids() const;

    // Propagate event effects through faction relationships
    void apply_event(std::string const &source_faction, int power_shift,
                     std::string const &target_faction);

  private:
    std::unordered_map<std::string, Faction> factions_;
};
