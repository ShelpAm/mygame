#pragma once

#include "entities/entity-manager.hpp"
#include <string>
#include <vector>

struct DialogueLine {
    enum Speaker { player, npc };
    Speaker speaker = npc;
    std::string text_key;
    std::string raw_text;
    bool use_raw = false;
    std::string npc_name;
};

struct NPCKnowledgeEntry {
    std::string fact_id;
    std::string version;
    int confidence = 70;
    bool witnessed = false;
    std::string source;
};

struct DialogueState {
    bool active = false;
    EntityId npc_entity = invalid_entity;
    std::string npc_id;
    std::string npc_name;
    std::vector<DialogueLine> history;
    std::vector<std::string> available_topics;
    std::vector<std::string> available_actions;
    int npc_trust = 0;
    int npc_fear = 0;
    bool can_trade = false;
    bool can_gift = false;
    bool can_threaten = false;
};
