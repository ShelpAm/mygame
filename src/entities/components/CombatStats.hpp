#pragma once

#include <string>

enum class Team { Player, Enemy, Neutral };

struct CombatStats {
    Team team = Team::Neutral;
    int maxHp = 10;
    int hp = 10;
    int attack = 2;
    int defense = 1;
    float attackRange = 96.f;
    float attackCooldown = 1.f;
    float cooldownRemaining = 0.f;
    bool alive = true;
};
