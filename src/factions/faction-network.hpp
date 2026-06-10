#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct Faction {
    std::string id;
    std::string name;
    int power = 50;        // 0-100, military/economic strength
    int cohesion = 50;     // 0-100, internal unity
    int wealth = 50;       // 0-100, material resources

    std::vector<std::string> controlled_locations;
    std::vector<std::string> member_npc_ids;
    std::string leader_npc_id;

    // Faction-to-faction relations: -100 (war) to 100 (allied)
    std::unordered_map<std::string, int> relations;
};

class FactionNetwork {
public:
    void add_faction(Faction faction);
    Faction* get_faction(const std::string& id);
    const Faction* get_faction(const std::string& id) const;

    void set_relation(const std::string& a, const std::string& b, int value);
    int get_relation(const std::string& a, const std::string& b) const;

    void modify_power(const std::string& id, int delta);
    void modify_cohesion(const std::string& id, int delta);
    void modify_wealth(const std::string& id, int delta);

    std::vector<std::string> all_faction_ids() const;

    // Propagate event effects through faction relationships
    void apply_event(const std::string& source_faction, int power_shift,
                    const std::string& target_faction);

private:
    std::unordered_map<std::string, Faction> factions_;
};
