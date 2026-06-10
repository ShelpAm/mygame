#pragma once

#include "entities/components/Position.hpp"
#include <string>
#include <vector>

struct DialogueLine {
    enum Speaker { Player, NPC };
    Speaker speaker = NPC;
    std::string textKey;
    std::string rawText;
    bool useRaw = false;
    std::string npcName;
};

struct NPCKnowledgeEntry {
    std::string factId;
    std::string version;
    int confidence = 70;
    bool witnessed = false;
    std::string source;
};

struct DialogueState {
    bool active = false;
    EntityId npcEntity = INVALID_ENTITY;
    std::string npcId;
    std::string npcName;
    std::vector<DialogueLine> history;
    std::vector<std::string> availableTopics;
    std::vector<std::string> availableActions;
    int npcTrust = 0;
    int npcFear = 0;
    bool canTrade = false;
    bool canGift = false;
    bool canThreaten = false;
};
