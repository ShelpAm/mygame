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

    void setRelation(const std::string& npcId, const Relation& rel);
    const Relation* getRelation(const std::string& npcId) const;
    Relation* getMutable(const std::string& npcId);

    void modifyTrust(const std::string& npcId, int delta);
    void modifyFear(const std::string& npcId, int delta);
    void modifyRespect(const std::string& npcId, int delta);

    std::vector<std::string> allNpcIds() const;

private:
    std::unordered_map<std::string, Relation> m_relations;
};
