#pragma once

#include <string>

struct SurvivalState {
    float food = 100.f;       // 0-100, decays ~5/day
    float water = 100.f;      // 0-100, decays ~7/day
    float health = 100.f;     // 0-100, decays when food/water at 0
    float energy = 100.f;     // 0-100, decays with activity, recovers with sleep

    float equipmentWear = 0.f; // 0-100

    static constexpr float FOOD_DECAY_PER_DAY = 5.f;
    static constexpr float WATER_DECAY_PER_DAY = 7.f;
    static constexpr float HEALTH_DECAY_STARVING = 10.f;  // per day when food==0 or water==0
    static constexpr float ENERGY_DECAY_MOVING = 2.f;     // per hour of movement
    static constexpr float ENERGY_RECOVER_SLEEPING = 15.f; // per hour of rest
};

class ConditionTracker {
public:
    void update(float gameHoursPassed, bool isMoving, bool isSleeping);

    void consumeFood(float amount);
    void consumeWater(float amount);
    void heal(float amount);

    const SurvivalState& state() const { return m_state; }

    bool isStarving() const { return m_state.food <= 0.f; }
    bool isDehydrated() const { return m_state.water <= 0.f; }
    bool isDead() const { return m_state.health <= 0.f; }

private:
    SurvivalState m_state;
};
