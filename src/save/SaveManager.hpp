#pragma once

#include "core/Math.hpp"
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

class WorldState;
class KnowledgeGraph;
class RelationshipTable;
class EntityManager;

class SaveManager {
public:
    struct NPCData {
        std::string id;
        std::string name;
        std::string personality;
        Vec2f position;
        int hp = 10; int maxHp = 10; bool alive = true;
        std::unordered_map<std::string, std::string> knowledge;  // factId -> version
    };

    struct SaveData {
        int day = 1;
        int season = 0;
        float timeOfDay = 6.f;
        Vec2f playerPos;
        int playerHp = 20; int playerMaxHp = 20;
        std::vector<std::string> knownTopics;
        std::vector<Vec2i> seenTiles;
        std::vector<std::pair<std::string, std::array<int, 3>>> relations;
        std::vector<NPCData> npcs;
    };

    static bool save(const std::string& path, const WorldState& ws,
                     const KnowledgeGraph& kg, const RelationshipTable& rt,
                     Vec2f playerPos, int playerHp, int playerMaxHp,
                     const std::vector<NPCData>& npcs);
    static bool load(const std::string& path, SaveData& out);
};
