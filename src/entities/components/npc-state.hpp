#pragma once

#include "knowledge/knowledge-graph.hpp"
#include <string>
#include <unordered_map>
#include <vector>

struct NPCState {
    std::string npc_id;
    std::string display_name;
    std::string personality; // "friendly", "guarded", "hostile", "fearful"

    // What this NPC knows
    struct KnownFact {
        std::string fact_id;
        std::string npc_version;
        int confidence = 50;
        bool witnessed = false;
        std::string source_npc_id;
    };
    std::unordered_map<std::string, KnownFact> knowledge;

    // Opinion toward player
    struct PlayerOpinion {
        int trust = 0;   // -100 to 100
        int fear = 0;    // 0 to 100
        int respect = 0; // 0 to 100
    };
    PlayerOpinion opinion;

    // Current agenda
    enum class Goal {
        gain_info,
        spread_misinfo,
        get_item,
        form_alliance,
        harm_player,
        neutral
    };
    Goal current_goal = Goal::neutral;
    std::string goal_fact_id;
    int urgency = 0; // 0-100
};
