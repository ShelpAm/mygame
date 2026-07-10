#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using EntityId = std::uint64_t; // matches flecs::entity_t
constexpr EntityId invalid_entity = 0;

// Fill [key] placeholders with values from slots map
inline std::string fill_template(std::string const &pattern,
                                 std::unordered_map<std::string, std::string> const &slots)
{
    std::string result = pattern;
    for (auto const &[key, value] : slots) {
        std::string placeholder = "[" + key + "]";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

struct DialogueLine {
    enum Speaker { player, npc };
    Speaker speaker = npc;
    std::string text_key;
    std::string raw_text;
    bool use_raw = false;
    std::string npc_name;

    // Template-based rendering (for client-side localization)
    bool use_template = false;
    std::string template_type;
    int variant_index = 0;
    std::unordered_map<std::string, std::string> slots;
};

struct NPCKnowledgeEntry {
    std::string fact_id;
    std::string locale_key; // e.g. "fact.ugarit_sack.merchant_1"
    std::string version;    // English fallback text
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

struct CombatEvent {
    EntityId attacker_id = 0;
    EntityId defender_id = 0;
    int damage = 0;
    bool killed = false;
};

enum class SessionMode : std::uint8_t { local, host, client };

struct AnimationClip {
    std::string name;
    std::vector<std::string> frame_sprites;  // keys into sprites.yaml
    std::vector<float> frame_durations;      // one per frame, in seconds
    bool faces_right = true;
};
