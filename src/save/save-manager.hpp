#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class WorldState;
class KnowledgeGraph;
class RelationshipTable;
class SaveManager {
  public:
    struct NPCData {
        std::string id;
        std::string name;
        std::string personality;
        Vec2f position;
        int hp = 10;
        int max_hp = 10;
        bool alive = true;
        std::unordered_map<std::string, std::string> knowledge; // fact_id -> version
    };

    struct SaveData {
        int day = 1;
        int season = 0;
        float time_of_day = 6.f;
        Vec2f player_pos;
        int player_hp = 20;
        int player_max_hp = 20;
        std::vector<std::string> known_topics;
        std::unordered_map<EntityId, std::vector<Vec2i>> player_explored_tiles;
        std::vector<std::pair<std::string, std::array<int, 3>>> relations;
        std::vector<NPCData> npcs;
    };

    static bool save(std::string const &path, WorldState const &ws, KnowledgeGraph const &kg,
                     RelationshipTable const &rt, Vec2f player_pos, int player_hp,
                     int player_max_hp, std::vector<NPCData> const &npcs);
    static bool load(std::string const &path, SaveData &out);
};
