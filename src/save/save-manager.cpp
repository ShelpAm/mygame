#include "save/save-manager.hpp"
#include "world/world-state.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "dialogue/relationship-table.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <iostream>

bool SaveManager::save(const std::string& path, const WorldState& ws,
                        const KnowledgeGraph& kg, const RelationshipTable& rt,
                        Vec2f player_pos, int player_hp, int player_max_hp,
                        const std::vector<NPCData>& npcs) {
    boost::json::object root;
    root["day"] = ws.day();
    root["season"] = ws.season();
    root["time_of_day"] = ws.time_of_day();
    root["playerX"] = player_pos.x;
    root["playerY"] = player_pos.y;
    root["player_hp"] = player_hp;
    root["player_max_hp"] = player_max_hp;

    // Known topics
    boost::json::array topicsArr;
    for (const auto& t : kg.known_topics()) {
        topicsArr.emplace_back(t);
    }
    root["known_topics"] = topicsArr;

    // Seen tiles
    boost::json::array tilesArr;
    for (const auto& t : ws.seen_tiles()) {
        boost::json::array tile;
        tile.emplace_back(t.x);
        tile.emplace_back(t.y);
        tilesArr.emplace_back(std::move(tile));
    }
    root["seen_tiles"] = tilesArr;

    // Relationships
    boost::json::array relArr;
    for (const auto& npc_id : rt.all_npc_ids()) {
        auto* rel = rt.get_relation(npc_id);
        if (!rel) continue;
        boost::json::object ro;
        ro["npc_id"] = npc_id;
        ro["trust"] = rel->trust;
        ro["fear"] = rel->fear;
        ro["respect"] = rel->respect;
        relArr.emplace_back(std::move(ro));
    }
    root["relationships"] = relArr;

    // NPCs
    boost::json::array npcsArr;
    for (const auto& npc : npcs) {
        boost::json::object no;
        no["id"] = npc.id;
        no["name"] = npc.name;
        no["personality"] = npc.personality;
        no["x"] = npc.position.x;
        no["y"] = npc.position.y;
        no["hp"] = npc.hp;
        no["max_hp"] = npc.max_hp;
        no["alive"] = npc.alive;

        boost::json::object knowledge;
        for (const auto& [fid, ver] : npc.knowledge) {
            knowledge[fid] = ver;
        }
        no["knowledge"] = knowledge;
        npcsArr.emplace_back(std::move(no));
    }
    root["npcs"] = npcsArr;

    std::ofstream file(path);
    if (!file) { std::cerr << "Failed to open save: " << path << '\n'; return false; }
    file << boost::json::serialize(root);
    return true;
}

bool SaveManager::load(const std::string& path, SaveData& out) {
    std::ifstream file(path);
    if (!file) return false;
    try {
        std::string content{std::istreambuf_iterator<char>(file), {}};
        auto root = boost::json::parse(content).as_object();
        out.day = static_cast<int>(root.at("day").as_int64());
        out.season = static_cast<int>(root.at("season").as_int64());
        out.time_of_day = static_cast<float>(root.at("time_of_day").as_double());
        out.player_pos = {static_cast<float>(root.at("playerX").as_double()),
                          static_cast<float>(root.at("playerY").as_double())};
        out.player_hp = static_cast<int>(root.at("player_hp").as_int64());
        out.player_max_hp = static_cast<int>(root.at("player_max_hp").as_int64());

        if (root.contains("known_topics"))
            for (const auto& t : root.at("known_topics").as_array())
                out.known_topics.emplace_back(t.as_string());

        if (root.contains("seen_tiles"))
            for (const auto& t : root.at("seen_tiles").as_array())
                out.seen_tiles.emplace_back(
                    (int)t.as_array()[0].as_int64(),
                    (int)t.as_array()[1].as_int64());

        if (root.contains("relationships"))
            for (const auto& r : root.at("relationships").as_array())
                out.relations.push_back({
                    std::string(r.as_object().at("npc_id").as_string()),
                    {{(int)r.as_object().at("trust").as_int64(),
                      (int)r.as_object().at("fear").as_int64(),
                      (int)r.as_object().at("respect").as_int64()}}
                });

        if (root.contains("npcs"))
            for (const auto& n : root.at("npcs").as_array()) {
                auto& obj = n.as_object();
                SaveManager::NPCData nd;
                nd.id = std::string(obj.at("id").as_string());
                nd.name = std::string(obj.at("name").as_string());
                nd.personality = std::string(obj.at("personality").as_string());
                nd.position = {(float)obj.at("x").as_double(), (float)obj.at("y").as_double()};
                nd.hp = (int)obj.at("hp").as_int64();
                nd.max_hp = (int)obj.at("max_hp").as_int64();
                nd.alive = obj.at("alive").as_bool();
                for (const auto& [k, v] : obj.at("knowledge").as_object())
                    nd.knowledge[std::string(k)] = std::string(v.as_string());
                out.npcs.push_back(std::move(nd));
            }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load: " << e.what() << '\n';
        return false;
    }
}
