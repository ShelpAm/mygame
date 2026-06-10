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

    std::vector<std::string> controlledLocations;
    std::vector<std::string> memberNpcIds;
    std::string leaderNpcId;

    // Faction-to-faction relations: -100 (war) to 100 (allied)
    std::unordered_map<std::string, int> relations;
};

class FactionNetwork {
public:
    void addFaction(Faction faction);
    Faction* getFaction(const std::string& id);
    const Faction* getFaction(const std::string& id) const;

    void setRelation(const std::string& a, const std::string& b, int value);
    int getRelation(const std::string& a, const std::string& b) const;

    void modifyPower(const std::string& id, int delta);
    void modifyCohesion(const std::string& id, int delta);
    void modifyWealth(const std::string& id, int delta);

    std::vector<std::string> allFactionIds() const;

    // Propagate event effects through faction relationships
    void applyEvent(const std::string& sourceFaction, int powerShift,
                    const std::string& targetFaction);

private:
    std::unordered_map<std::string, Faction> m_factions;
};
