#include "survival/ConditionTracker.hpp"
#include <algorithm>

void ConditionTracker::update(float gameHoursPassed, bool isMoving, bool isSleeping) {
    float daysPassed = gameHoursPassed / 24.f;

    // Decay food and water
    m_state.food -= SurvivalState::FOOD_DECAY_PER_DAY * daysPassed;
    m_state.water -= SurvivalState::WATER_DECAY_PER_DAY * daysPassed;

    // Starvation/dehydration damages health
    if (m_state.food <= 0.f) {
        m_state.health -= SurvivalState::HEALTH_DECAY_STARVING * daysPassed;
    }
    if (m_state.water <= 0.f) {
        m_state.health -= SurvivalState::HEALTH_DECAY_STARVING * daysPassed;
    }

    // Energy
    if (isSleeping) {
        m_state.energy += SurvivalState::ENERGY_RECOVER_SLEEPING * gameHoursPassed;
    } else if (isMoving) {
        m_state.energy -= SurvivalState::ENERGY_DECAY_MOVING * gameHoursPassed;
    } else {
        m_state.energy -= 0.5f * gameHoursPassed;
    }

    // Equipment wear from movement
    if (isMoving) {
        m_state.equipmentWear += 0.5f * daysPassed;
    }

    // Clamp
    m_state.food = std::clamp(m_state.food, 0.f, 100.f);
    m_state.water = std::clamp(m_state.water, 0.f, 100.f);
    m_state.health = std::clamp(m_state.health, 0.f, 100.f);
    m_state.energy = std::clamp(m_state.energy, 0.f, 100.f);
    m_state.equipmentWear = std::clamp(m_state.equipmentWear, 0.f, 100.f);
}

void ConditionTracker::consumeFood(float amount) {
    m_state.food = std::clamp(m_state.food + amount, 0.f, 100.f);
}

void ConditionTracker::consumeWater(float amount) {
    m_state.water = std::clamp(m_state.water + amount, 0.f, 100.f);
}

void ConditionTracker::heal(float amount) {
    m_state.health = std::clamp(m_state.health + amount, 0.f, 100.f);
}
