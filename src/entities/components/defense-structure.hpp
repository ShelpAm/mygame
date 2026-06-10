#pragma once

#include <string>

struct DefenseStructure {
    enum class Type { watchtower, wall, barricade };
    Type type = Type::watchtower;
    int defense_value = 10;   // Reduces incoming raid damage
    int max_hp = 50;
    int hp = 50;
    float range = 300.f;     // Auto-attack range against raiders
    int damage = 3;
    std::string location_id;
};
