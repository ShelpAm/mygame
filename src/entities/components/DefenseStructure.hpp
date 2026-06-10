#pragma once

#include <string>

struct DefenseStructure {
    enum class Type { Watchtower, Wall, Barricade };
    Type type = Type::Watchtower;
    int defenseValue = 10;   // Reduces incoming raid damage
    int maxHp = 50;
    int hp = 50;
    float range = 300.f;     // Auto-attack range against raiders
    int damage = 3;
    std::string locationId;
};
