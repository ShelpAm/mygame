#include "survival/condition-tracker.hpp"
#include <algorithm>

void ConditionTracker::update(float game_hours_passed, bool is_moving,
                              bool is_sleeping)
{
    float days_passed = game_hours_passed / 24.f;

    // Decay food and water
    state_.food -= SurvivalState::food_decay_per_day * days_passed;
    state_.water -= SurvivalState::water_decay_per_day * days_passed;

    // Starvation/dehydration damages health
    if (state_.food <= 0.f) {
        state_.health -= SurvivalState::health_decay_starving * days_passed;
    }
    if (state_.water <= 0.f) {
        state_.health -= SurvivalState::health_decay_starving * days_passed;
    }

    // Energy
    if (is_sleeping) {
        state_.energy +=
            SurvivalState::energy_recover_sleeping * game_hours_passed;
    }
    else if (is_moving) {
        state_.energy -= SurvivalState::energy_decay_moving * game_hours_passed;
    }
    else {
        state_.energy -= 0.5f * game_hours_passed;
    }

    // Equipment wear from movement
    if (is_moving) {
        state_.equipment_wear += 0.5f * days_passed;
    }

    // Clamp
    state_.food = std::clamp(state_.food, 0.f, 100.f);
    state_.water = std::clamp(state_.water, 0.f, 100.f);
    state_.health = std::clamp(state_.health, 0.f, 100.f);
    state_.energy = std::clamp(state_.energy, 0.f, 100.f);
    state_.equipment_wear = std::clamp(state_.equipment_wear, 0.f, 100.f);
}

void ConditionTracker::consume_food(float amount)
{
    state_.food = std::clamp(state_.food + amount, 0.f, 100.f);
}

void ConditionTracker::consume_water(float amount)
{
    state_.water = std::clamp(state_.water + amount, 0.f, 100.f);
}

void ConditionTracker::heal(float amount)
{
    state_.health = std::clamp(state_.health + amount, 0.f, 100.f);
}
