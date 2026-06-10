#pragma once

#include <string>

enum class Team { player, enemy, neutral };

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
};
