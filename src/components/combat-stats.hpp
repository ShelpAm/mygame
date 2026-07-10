#pragma once

#include <cstdint>
#include <format>
#include <string_view>

// using Team = uint8_t;
enum class Team : uint8_t {
    invalid_team = 0,
    neutral = 1,
    player_begin = 2,
    player_end = 100, // exclusive
    enemy = player_end,
};

template <> struct std::formatter<Team> : std::formatter<std::string_view> {
    auto format(Team t, std::format_context &ctx) const
    {
        using enum Team;
        std::string name = "unknown";
        switch (t) {
        case invalid_team:
            name = "invalid_team";
            break;
        case Team::neutral:
            name = "neutral";
            break;
        case Team::enemy:
            name = "enemy";
            break;
        default:
            name = "player-team-" + std::to_string(static_cast<std::uint8_t>(t));
        }
        return std::formatter<std::string_view>::format(name, ctx);
    }
};

inline bool is_hostile(Team a, Team b)
{
    return a != Team::neutral && b != Team::neutral && a != b;
}

struct CombatStats {
    Team team = Team::neutral;
    int max_hp = 10;
    int hp = 10;
    int attack = 2;
    int defense = 1;
    float attack_range = 96.f;
    float attack_cooldown = 1.f;
    float cooldown_remaining = 0.f;
    bool alive = true;

    // hp(4) + max_hp(4) + alive(1) + team(1) + attack(4) + defense(4) +
    // attack_range(4) = 22 bytes
    static constexpr uint16_t kSyncWireSize = 22;
};
