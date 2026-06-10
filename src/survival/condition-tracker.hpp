#pragma once

#include <string>

struct SurvivalState {
    float food = 100.f;   // 0-100, decays ~5/day
    float water = 100.f;  // 0-100, decays ~7/day
    float health = 100.f; // 0-100, decays when food/water at 0
    float energy = 100.f; // 0-100, decays with activity, recovers with sleep

    float equipment_wear = 0.f; // 0-100

    static constexpr float food_decay_per_day = 5.f;
    static constexpr float water_decay_per_day = 7.f;
    static constexpr float health_decay_starving =
        10.f; // per day when food==0 or water==0
    static constexpr float energy_decay_moving = 2.f; // per hour of movement
    static constexpr float energy_recover_sleeping = 15.f; // per hour of rest
};

class ConditionTracker {
  public:
    void update(float game_hours_passed, bool is_moving, bool is_sleeping);

    void consume_food(float amount);
    void consume_water(float amount);
    void heal(float amount);

    SurvivalState const &state() const
    {
        return state_;
    }

    bool is_starving() const
    {
        return state_.food <= 0.f;
    }
    bool is_dehydrated() const
    {
        return state_.water <= 0.f;
    }
    bool is_dead() const
    {
        return state_.health <= 0.f;
    }

  private:
    SurvivalState state_;
};
