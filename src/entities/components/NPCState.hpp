#pragma once

#include "knowledge/KnowledgeGraph.hpp"
#include <string>
#include <vector>
#include <unordered_map>

struct NPCState {
    std::string npcId;
    std::string displayName;
    std::string personality;  // "friendly", "guarded", "hostile", "fearful"

    // What this NPC knows
    struct KnownFact {
        std::string factId;
        std::string npcVersion;
        int confidence = 50;
        bool witnessed = false;
        std::string sourceNpcId;
    };
    std::unordered_map<std::string, KnownFact> knowledge;

    // Opinion toward player
    struct PlayerOpinion {
        int trust = 0;     // -100 to 100
        int fear = 0;      // 0 to 100
        int respect = 0;   // 0 to 100
    };
    PlayerOpinion opinion;

    // Current agenda
    enum class Goal { GainInfo, SpreadMisinfo, GetItem, FormAlliance, HarmPlayer, Neutral };
    Goal currentGoal = Goal::Neutral;
    std::string goalFactId;
    int urgency = 0;  // 0-100
};
